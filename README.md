# memory submodule

This repository is a [git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules)
providing C++ memory related utilities for larger projects, including:

* ``SimpleSegregatedStorage`` : Maintains an unordered free list of blocks (used by NodeMemoryResource and MemoryPagePool).
* ``MemoryPagePool`` : A memory pool that returns fixed-size memory blocks allocated with ``std::aligned_alloc`` and aligned to ``memory_page_size``.
* ``NodeMemoryPool`` : A memory pool intended for fixed size allocations, one object at a time, where the size and type of the object are not known until the first allocation. Intended to be used with ``std::allocate_shared`` or ``std::list``.
* ``NodeMemoryResource`` : A fixed size memory resource that uses a ``MemoryPagePool`` as upstream.
* ``DequeAllocator`` : The perfect allocator for your deque's.

## Prerequisites

The root project should be using
[cmake](https://cmake.org/overview/),
[cwm4](https://github.com/CarloWood/cwm4),
[cwds](https://github.com/CarloWood/cwds) and
[utils](https://github.com/CarloWood/ai-utils).

The following aicxx submodules require this submodule:
* [events](https://github.com/CarloWood/events)
* [evio](https://github.com/CarloWood/evio)
* [statefultask](https://github.com/CarloWood/ai-statefultask) (which depends on evio)
* [resolver-task](https://github.com/CarloWood/resolver-task) (for which you'd need statefultask anyway)

# HISTORY

The utilities in this submodule used to part of the submodule ``utils``.
If you have a project that was using any of the above directly (as opposed to using statefultask),
then you need to change the initialization at the top of ``main``.

Change the ``utils::`` prefix for any of ``MemoryPagePool``, ``NodeMemoryPool``, ``NodeMemoryResource`` and/or
``DequeAllocator`` into ``memory::``. For example, ``utils::MemoryPagePool mpp(0x8000);`` now becomes
``memory::MemoryPagePool mpp(0x8000);``.

See for example the usage documentation at the top of [DequeAllocator](https://github.com/CarloWood/memory/blob/master/DequeAllocator.h#L17)
more for initialization details.

## Adding the memory submodule to a project

To add this submodule to a project, that project should already
be set up to use [cwm4](https://github.com/CarloWood/cwm4).

Simply execute the following in the root directory of that project:

    git submodule add https://github.com/CarloWood/memory.git

This should clone memory into the subdirectory ``memory``, or
if you already cloned it there, it should add it.

Make sure to read the [README](https://github.com/CarloWood/ai-utils?tab=readme-ov-file#checking-out-a-project-that-uses-the-ai-utils-submodule) of ``utils`` for general buildsystem information.
