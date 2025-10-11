#include "mmap/mmap_aux.h"

#include <windows.h>

namespace logger {

//先写一个自定义删除器        这里主要是利用智能指针的RAII机制，所以定义一个这个东西
class FileHeadleDelete {
public:
    void operator()(HANDLE h) {
        if (h != NULL) {
            CloseHandle(h);
        }
    }
};

//挂个原理，以防未来的我不知道
//在 Windows 编程中，使用 typedef void* HANDLE;而非直接使用 void*，以及进一步在智能指针中使用 std::remove_pointer_t<HANDLE>，
//是一套深思熟虑的设计组合，主要目的在于​​提升代码的抽象级别、增强类型安全，并为现代 C++ 的 RAII 范式提供无缝支持​​
//PS：他其实需要更深入的理解，等有时间了记得仔细琢磨一下

using FileHeadlePtr = std::unique_ptr<std::remove_pointer_t<HANDLE>,FileHeadleDelete>;
//智能指针能够有俩参数，第一个参数需要时具体的类型，第二个参数可以是一个自定义删除器

bool MMapAux::TryMap_(size_t capacity) {

    FileHeadlePtr file_handle(CreateFileW(file_path_.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    if (file_handle.get() == INVALID_HANDLE_VALUE) {                //INVALID_HANDLE_VALUE是win端定义，实质为-1
        return false;
    }

    FileHeadlePtr file_mmaping_handle;
    file_mmaping_handle.reset(CreateFileMapping(file_handle.get(), NULL, PAGE_READWRITE, 0, capacity, NULL));
    if (!file_mmaping_handle) {
        return false;
    }

    handle_ = MapViewOfFile(file_mmaping_handle.get(), FILE_MAP_ALL_ACCESS, 0, 0, capacity);
    return handle_ != NULL;

}

void MMapAux::Unmap_() {
    if (handle_) {
        UnmapViewOfFile(handle_);
    }
    handle_ = NULL;
}

void MMapAux::Sync_() {
    if (handle_) {
        FlushViewOfFile(handle_, capacity_);
    }
}


}
