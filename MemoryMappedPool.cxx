/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Definition of class MemoryMappedPool.
 *
 * @Copyright (C) 2025  Carlo Wood.
 *
 * pub   dsa3072/C155A4EEE4E527A2 2018-08-16 Carlo Wood (CarloWood on Libera) <carlo@alinoe.com>
 * fingerprint: 8020 B266 6305 EE2F D53E  6827 C155 A4EE E4E5 27A2
 *
 * This file is part of memory.
 *
 * memory is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * memory is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with memory.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sys.h"
#include "MemoryMappedPool.h"
#include "utils/AIAlert.h"
#include "utils/at_scope_end.h"
#include "utils/to_string.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

namespace memory {

#ifdef CWDEBUG
namespace {

std::string print_prot(int prot)
{
  std::string result;
  bool empty = true;
  if ((prot & PROT_READ))
  {
    result = "PROT_READ";
    empty = false;
    prot &= ~PROT_READ;
  }
  if ((prot & PROT_WRITE))
  {
    if (!empty)
      result += "|";
    result += "PROT_WRITE";
    empty = false;
    prot &= ~PROT_WRITE;
  }
  if ((prot & PROT_EXEC))
  {
    if (!empty)
      result += "|";
    result += "PROT_EXEC";
    empty = false;
    prot &= ~PROT_EXEC;
  }
  ASSERT(prot == PROT_NONE);
  if (empty)
    result = "PROT_NONE";
  return result;
}

std::string print_flags(int flags)
{
  std::string result;
  bool empty = true;
  if ((flags & MAP_PRIVATE))
  {
    result = "MAP_PRIVATE";
    empty = false;
    flags &= ~MAP_PRIVATE;
  }
  if ((flags & MAP_SHARED))
  {
    if (!empty)
      result += "|";
    result += "MAP_SHARED";
    empty = false;
    flags &= ~MAP_SHARED;
  }
  if ((flags & MAP_FIXED))
  {
    if (!empty)
      result += "|";
    result += "MAP_FIXED";
    empty = false;
    flags &= ~MAP_FIXED;
  }
  ASSERT(flags == 0);
  if (empty)
    result = "0";
  return result;
}

} // namespace
#endif

MemoryMappedPool::MemoryMappedPool(std::filesystem::path const& filename, size_t block_size, size_t file_size,
    Mode mode, bool zero_init) : MemoryPagePoolBase(block_size), mapped_base_(MAP_FAILED)
{
  DoutEntering(dc::notice, "MemoryMappedPool::MemoryMappedPool(" << filename << ", " << block_size << ", " << file_size <<
      ", " << utils::to_string(mode) << std::boolalpha << zero_init << ") [" << this << "]");

  // block_size must be capable of containing a FreeNode.
  ASSERT(block_size >= sizeof(typename PtrTag::FreeNode));

  // block_size must be a multiple of memory_page_size (and larger than 0).
  ASSERT(block_size % memory_page_size() == 0);

  namespace fs = std::filesystem;

  // The file_size must be a multiple of memory_page_size.
  ASSERT(file_size % memory_page_size() == 0);

  // The following possibilities exist:
  //
  //  .---- File does not (N) exist (or not readable) - ⎫
  //  |---- File exists and is only readable (R)      - ⎬ mutually exclusive
  //  |---- File exists and is writable (W)           - ⎭
  //  |.--- File size is given
  //  ||.-- Persistence is requested (P)
  //  |||-- Data is (requested as) read-only          - ⎫
  //  |||-- Do copy-on-write (C)                        ⎬ Can not be on at
  //  |||.- Zero initialization is requested (Z)      - ⎭ the same time
  //  ||||
  //  NSP0
  //  NSPZ
  //  R0C0
  //  R0R0
  //  RSC0 Provided file_size must match existing file
  //  RSR0 Provided file_size must match existing file
  //  W0C0
  //  W0CZ
  //  W0P0
  //  W0PZ
  //  W0R0
  //  WSC0 Provided file_size must match existing file
  //  WSCZ Provided file_size must match existing file
  //  WSP0 Provided file_size must match existing file
  //  WSPZ Provided file_size must match existing file
  //  WSR0 Provided file_size must match existing file
  //
  //  If the file doesn't exist one can not request the data to be read-only. Therefore
  //  N-R- is not possible.
  //  If the data is requested as read-only then it makes no sense to request zero initialization. Therefore
  //  --RZ is not possibe.
  //  Persistence requires writing to the file, therefore
  //  R-P- is not possible.
  //  If the file doesn't exist then the file size must be given. Therefore
  //  N0-- is not possible.
  //  A provided file_size must match an existing file's size. This is the case for WS-- and RS--.
  //  If the file is read-only then it makes no sense to request zero initialization. Therefore
  //  R--Z is not possible.
  //  If the mode is copy_on_write then the file must exist. You cannot copy-on-write a file that doesn't exist. Therefore
  //  N-C- is not possible.

  // Do not pass Mode::read_only together with zero_init.
  ASSERT(mode != Mode::read_only || !zero_init);

  // Get the absolute file path.
  std::filesystem::path absolute_file_path = fs::absolute(filename);

  // Get information about the possibly already existing file.
  fs::file_status file_status = fs::status(absolute_file_path);

  bool const file_exists = file_status.type() != fs::file_type::not_found;
  bool const file_is_regular_file = file_status.type() == fs::file_type::regular;
  bool const is_readable = file_is_regular_file &&
    (file_status.permissions() & (fs::perms::owner_read|fs::perms::group_read|fs::perms::others_read)) != fs::perms::none;
  bool const is_writable = file_is_regular_file &&
    (file_status.permissions() & (fs::perms::owner_write|fs::perms::group_write|fs::perms::others_write)) != fs::perms::none;

  std::string error;
  if (file_exists && (!file_is_regular_file || !is_readable))
    // If a filename is provided, it must be a readable, regular file.
    error = std::string("File exists but is not ") + (!file_is_regular_file ? "a regular file" : "readable") + ": [FILEPATH]!";
  else if (!file_exists)
  {
    if (file_size == 0)                                 // N0-- is not possible.
      // If the file doesn't exist then the file size must be given.
      error = "The file [FILEPATH] does not exist, and no size was provided.";
    else if (mode == Mode::read_only)                   // N-R- is not possible.
      // If the file doesn't exist one can not request the data to be read-only.
      error = "No such file: [FILEPATH]";
    else if (mode == Mode::copy_on_write)               // N-C- is not possible.
      // If the mode is copy_on_write then the file must exist. You cannot copy-on-write a file that doesn't exist.
      error = "Copy-on-write was requested, but the file [FILEPATH] does not exist!";
  }
  else if (!is_writable)
  {
    if (mode == Mode::persistent)                       // R-P- is not possible.
      // Persistence requires writing to the file.
      error = "Persistent mode requested, but file [FILEPATH] is not writable.";
    else if (zero_init)                                 // R--Z is not possible.
      // If the file is read-only then it makes no sense to request zero initialization.
      error = "Zero initialization requested for read-only file [FILEPATH].";
  }
  if (!error.empty())
    THROW_LALERT(error, AIArgs("[FILEPATH]", absolute_file_path));

  int fd = -1;
  auto&& close_fd = at_scope_end([&fd]{ if (fd != -1) ::close(fd); });

  if (!file_exists)
  {
    // NSP0 (which is also zero initialized).
    // NSPZ
    //
    // Try to create the file.
    fd = ::open(absolute_file_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd == -1)
      THROW_LALERTE("Failed to create file [FILEPATH]", AIArgs("[FILEPATH]", absolute_file_path));

    struct stat s_stat;
    if (::fstat(fd, &s_stat) == -1)
      THROW_LALERTE("fstat([FD])", AIArgs("[FD]", fd));
    else if (s_stat.st_size != 0)
      THROW_LALERT("Previously non-existing file [FILEPATH] has size [FILESIZE] after opening it?!",
          AIArgs("[FILEPATH]", absolute_file_path)("[FILESIZE]", file_size));

    // Allocate disk space, this assures that there will be enough disk space available
    // if this succeeds. Any subregion within the range specified by offset and size
    // that did not contain data before the call will be initialized to zero. In our
    // case that everything because file was just created and has size zero.
    if (::fallocate(fd, 0, 0, file_size) == -1)
    {
      THROW_LALERTE("Failed to allocate [FILESIZE] bytes for file [FILEPATH]",
          AIArgs("[FILEPATH]", absolute_file_path)("[FILESIZE]", file_size));
    }

    // This is now the size of the file.
    mapped_size_ = file_size;
  }
  else
  {
    // Open the existing file.
    int flags = O_RDONLY;
    mode_t m;
    if (mode == Mode::persistent)
    {
      // W-P0
      // W-PZ
      // In persistent mode we open the file with the capability to write to it.
      flags = O_RDWR;
      m = 0644;
    }
    else
    {
      // R-C0
      // R-R0
      // W-C0
      // W-R0
      // W-CZ
      m = 0444;
    }
    fd = ::open(absolute_file_path.c_str(), flags, m);

    // Set the correct mapped_size_.
    struct stat s_stat;
    if (::fstat(fd, &s_stat) == -1)
      THROW_LALERTE("fstat([FD])", AIArgs("[FD]", fd));
    if (file_size == 0)
    {
      if (s_stat.st_size % memory_page_size() != 0)
        THROW_LALERT("The size of existing file [FILEPATH] ([FILESIZE]) is not a multiple of the memory page size ([PAGESIZE]).",
            AIArgs("[FILEPATH]", absolute_file_path)("[FILESIZE]", s_stat.st_size)("[PAGESIZE]", memory_page_size()));
      // Use actual file size.
      mapped_size_ = s_stat.st_size;
    }
    else if (s_stat.st_size != file_size)
      THROW_LALERT("Provided file size ([FILESIZE]) does not match the size of existing file [FILEPATH] ([ACTUALSIZE] bytes).",
          AIArgs("[FILEPATH]", absolute_file_path)("[FILESIZE]", file_size)("[ACTUALSIZE]", s_stat.st_size));
    else
      mapped_size_ = file_size;

    if (mode == Mode::persistent && zero_init)
    {
      // W-PZ
      //
      // Preallocated blocks for regions that span the holes in the file, this
      // assures that there will be enough disk space available if this succeeds.
      // After a successful call, subsequent reads from this range will return zeros.
      // Note that zeroing is done within the filesystem preferably by converting
      // the range into unwritten extents. This approach means that the specified
      // range will not be physically zeroed out on the device, and I/O is required
      // only to update metadata.
      //
      if (::fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, mapped_size_) == -1)
        THROW_LALERTE("Failed to zero existing file [FILEPATH]",
            AIArgs("[FILEPATH]", absolute_file_path));
    }
  }

  // Map the file to virtual memory.

  // Determine the protection and flags for mmap based on the requested mode.
  int prot = PROT_READ|PROT_WRITE;
  int flags = MAP_PRIVATE;

  if (mode == Mode::persistent)
    flags = MAP_SHARED;
  else if (mode == Mode::read_only)
    prot = PROT_READ;

  // Map the file into the process's virtual address space.
  Dout(dc::system|continued_cf, "::mmap(nullptr, " << mapped_size_ << ", " << print_prot(prot) << ", " <<
      print_flags(flags) << ", " << fd << ", 0) = ");
  mapped_base_ = ::mmap(nullptr, mapped_size_, prot, flags, fd, 0);
  Dout(dc::finish, mapped_base_);

  // Check for errors.
  if (mapped_base_ == MAP_FAILED)
    THROW_LALERTE("Failed to map file [FILEPATH] of size [SIZE]",
        AIArgs("[FILEPATH]", absolute_file_path)("[SIZE]", mapped_size_));

  // Set head_tag_ to point to the start of mapped memory.
  mss_.initialize(mapped_base_);
}

MemoryMappedPool::~MemoryMappedPool()
{
  DoutEntering(dc::notice, "MemoryMappedPool::~MemoryMappedPool() [" << this << "]");
  if (mapped_base_ != MAP_FAILED)
    ::munmap(mapped_base_, mapped_size_);
}

} // namespace memory
