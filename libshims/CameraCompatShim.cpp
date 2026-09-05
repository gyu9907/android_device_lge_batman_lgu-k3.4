/* Minimal ABI bridge for the Jelly Bean msm8660 camera blob. */

#include <stddef.h>
#include <sys/types.h>

#define LEGACY_VECTOR_SYMBOL(name, symbol) \
    extern "C" __attribute__((visibility("default"))) void name(void *) \
            __asm__(symbol); \
    extern "C" void name(void *) { }

LEGACY_VECTOR_SYMBOL(vector_impl_1, "_ZN7android10VectorImpl19reservedVectorImpl1Ev")
LEGACY_VECTOR_SYMBOL(vector_impl_2, "_ZN7android10VectorImpl19reservedVectorImpl2Ev")
LEGACY_VECTOR_SYMBOL(vector_impl_3, "_ZN7android10VectorImpl19reservedVectorImpl3Ev")
LEGACY_VECTOR_SYMBOL(vector_impl_4, "_ZN7android10VectorImpl19reservedVectorImpl4Ev")
LEGACY_VECTOR_SYMBOL(vector_impl_5, "_ZN7android10VectorImpl19reservedVectorImpl5Ev")
LEGACY_VECTOR_SYMBOL(vector_impl_6, "_ZN7android10VectorImpl19reservedVectorImpl6Ev")
LEGACY_VECTOR_SYMBOL(vector_impl_7, "_ZN7android10VectorImpl19reservedVectorImpl7Ev")
LEGACY_VECTOR_SYMBOL(vector_impl_8, "_ZN7android10VectorImpl19reservedVectorImpl8Ev")
LEGACY_VECTOR_SYMBOL(sorted_vector_impl_1, "_ZN7android16SortedVectorImpl25reservedSortedVectorImpl1Ev")
LEGACY_VECTOR_SYMBOL(sorted_vector_impl_2, "_ZN7android16SortedVectorImpl25reservedSortedVectorImpl2Ev")
LEGACY_VECTOR_SYMBOL(sorted_vector_impl_3, "_ZN7android16SortedVectorImpl25reservedSortedVectorImpl3Ev")
LEGACY_VECTOR_SYMBOL(sorted_vector_impl_4, "_ZN7android16SortedVectorImpl25reservedSortedVectorImpl4Ev")
LEGACY_VECTOR_SYMBOL(sorted_vector_impl_5, "_ZN7android16SortedVectorImpl25reservedSortedVectorImpl5Ev")
LEGACY_VECTOR_SYMBOL(sorted_vector_impl_6, "_ZN7android16SortedVectorImpl25reservedSortedVectorImpl6Ev")
LEGACY_VECTOR_SYMBOL(sorted_vector_impl_7, "_ZN7android16SortedVectorImpl25reservedSortedVectorImpl7Ev")
LEGACY_VECTOR_SYMBOL(sorted_vector_impl_8, "_ZN7android16SortedVectorImpl25reservedSortedVectorImpl8Ev")

extern "C" void memory_base_current(void *, void *, ssize_t, size_t)
        __asm__("_ZN7android10MemoryBaseC1ERKNS_2spINS_11IMemoryHeapEEEij");

extern "C" __attribute__((visibility("default")))
void memory_base_legacy(void *object, void *heap, long offset,
        unsigned int size)
        __asm__("_ZN7android10MemoryBaseC1ERKNS_2spINS_11IMemoryHeapEEElj");

extern "C" void memory_base_legacy(void *object, void *heap, long offset,
        unsigned int size)
{
    memory_base_current(object, heap, static_cast<ssize_t>(offset), size);
}
