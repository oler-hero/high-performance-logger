#include "mmap/mmap_aux.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "defer.h"

namespace logger {

bool MMapAux::TryMap_(size_t capacity) {
    //为open函数提供兼容的路径参数,这边先从fielsystem降级到string再降级到c风格字符串
    int fd = open(file_path_.string().c_str(), O_RDWR | O_CREAT, S_IRWXU);        //注意这里的读写不一定能用，后续看一下

    LOG_DEFER {
        if (fd != -1) {
            close(fd);
        }
    };

    if (fd == -1) {
        return false;
    }else {
        ftruncate(fd, capacity);
    }

    handle_ = mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return handle_ != MAP_FAILED;

}

void MMapAux::Unmap_() {
    if (handle_) {
        munmap(handle_, capacity_);
    }
    handle_ = NULL;                 //标记一种状态
}

void MMapAux::Sync_() {
    if(handle_) {
        msync(handle_, capacity_, MS_SYNC);              //MS_SYNC选择同步:对于需要数据持久化的场景（如日志系统）
                                                            //MS_SYNC能确保数据不会因系统崩溃而丢失
    }   
}


}

