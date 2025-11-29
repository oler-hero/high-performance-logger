#include "mmap/mmap_aux.h"

#include <string.h>

#include "utils/file_util.h"
#include "utils/sys_util.h"

//根据依赖顺序开发

namespace logger {

//空间不能太小，不然映射等于白映射了
static constexpr size_t kDefaultCapacity = 512 * 1024;    //512kb     减少重映射开销​​



MMapAux::MMapAux(fpath file_path) : file_path_(std::move(file_path)),capacity_(0),handle_(nullptr) {
    //除了初始化数据，还要映射（因为咱们是mmap），还要初始化。必须要先映射再初始化。
                                                        //原因：顺序至关重要：映射搭建了“通道”，初始化则是在通道畅通后进行的“内部装修”
    size_t file_size = fs::GetFileSize(file_path_);
    size_t dst_size = std::max(file_size,kDefaultCapacity);
    Reserve_(dst_size);
    Init_();

}





MMapAux::MmapHeader* MMapAux::Header_() const {
    if (!handle_) {                                                 //空指针检查
        return nullptr;
    }
    if (capacity_ < sizeof(MmapHeader)) {                           //容量大小检查
        return nullptr;
    }
    return static_cast<MmapHeader*>(handle_);
}


bool MMapAux::IsValid_() const {
    MmapHeader* header = Header_();

    if (!handle_) {             //检查空指针，防御编程
        return false;           //访问无效指针时发生崩溃或产生未定义行为
    }
    
    return MmapHeader::kMagic == header->magic;      //直接省去一步，优化性能，不用直接return true和false，而是直接返回这种判断

}


void MMapAux::Init_() {
    MmapHeader* header = Header_();     // 获取头结构指针
    if (!header) {
        return ;    //获取失败，直接返回
    }
    if (header->magic != MmapHeader::kMagic) {
        header->magic = MmapHeader::kMagic;     // 设置魔术数字
        header->size = 0;                       // 初始化数据大小为0
    }

}


double MMapAux::GetRatio() const {
    if (!IsValid_()) {
        return 0.0;
    }

    return static_cast<double>(Size()) / (Capacity_() - sizeof(MmapHeader));

}

void MMapAux::Clear() {
    if (!IsValid_()) {
        return ;
    }

   Header_()->size = 0;         //它直接利用函数返回的指针进行成员访问，是语法所允许的标准操作

}


size_t MMapAux::Size() const {
    if (!IsValid_()) {
        return 0;
    }

    return Header_()->size;

}


uint8_t* MMapAux::Data() const{
    if (!IsValid_()) {
        return nullptr;
    }

    return static_cast<uint8_t*>(handle_) + (sizeof(MmapHeader));           //因为直接指向数据的头指针，所以需要加偏移量


}

//一个仅限当前文件能看到的辅助函数，帮助页面数以及向上取整
static size_t GetValueCapacity(size_t size) {

    size_t page_size = GetPageSize();
    //向上取整算法
    return (size + page_size - 1) / page_size * page_size;//建议详细学习一下

}



void MMapAux::Reserve_(size_t new_size) {
    if (new_size < capacity_) {
        return ;
    }

    new_size = GetValueCapacity(new_size);

    if (new_size == capacity_) {        //把new_size变成页面数的整数倍之后可能会相同，所以再做一次判断
        return ;
    }

    Unmap_();
    TryMap_(new_size);
    capacity_ = new_size;

}


void MMapAux::EnsureCapacity_(size_t new_size) {  //new_size传入的是数据的大小
    
    size_t real_size = new_size + sizeof(MmapHeader);
    if (real_size <= capacity_) {
        return ;
    }

    auto dst_capacity = capacity_;

    while (dst_capacity < real_size) {
        dst_capacity += GetPageSize();             //获取页大小的，别被fs::那个误导了，那个是utils里获取文件大小的
    }

    Reserve_(dst_capacity);         //其实reserve内部实现了capacity的更新，使用dst无副作用
                                    //(指不用dst，用原来的capacity)提前再这个函数中修改capacity的副作用：如果Reserve_()失败，capacity_可能处于不一致状态
}


void MMapAux::Resize(size_t new_size) {         //resize的size是一定会改变的
    if (!IsValid_()) {
        return ;
    }

    //此处不用考虑扩容和缩容，因为en已经考虑过了

    EnsureCapacity_(new_size);      //扩容正常，缩容他会直接跳出来（高效）
    Header_()->size = new_size;     //更新实际size

}

void MMapAux::Push(const void* data,size_t size) {
    // if (!IsValid_()) {
    //     return ;
    // }
    // EnsureCapacity_(Header_()->size + size);
    // memcpy(handle_ + sizeof(MmapHeader) + Size(),data,size);
    // Header_()->size += size;

    //进行优化,这里主要是为了提醒一下导师，我所有的东西都是自己实现过之后，再一个个看过来更正的，前面没有更正的痕迹，我怕导师误会我抄代码

    if (!IsValid_()) {
        return ;
    }

    size_t new_size = Size() + size;                //清晰的结构，便于阅读
    EnsureCapacity_(new_size);                      //确保容量足够
    memcpy(Data() + Size(), data, size);            //清晰的利用到了接口
    Header_()->size = new_size;                     //更新，因为是结构体的普通成员，所以不能mmapheader::这样来，只能利用header()来帮助我们
}


}

