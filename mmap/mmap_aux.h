#pragma once

#include<filesystem>
#include<memory>

namespace logger {
    
class MMapAux {
public:  
using fpath = std::filesystem::path;

explicit MMapAux(fpath file_path);

~MMapAux() = default;               //为什么默认，因为后续会实现一个dffer模块

MMapAux(const MMapAux&) = delete;   //禁止拷贝构造和赋值操作​​，因为内存映射资源不能被复制

MMapAux& operator=(const MMapAux&) = delete;

void Resize(size_t new_size);       //调整映射文件的大小，new_size必须考虑内存页对齐,直接更改size的

uint8_t* Data() const;              //返回指向​​数据区域起始位置​​的指针（跳过文件头）&& uint8_t*​​：字节指针，便于按字节操作

size_t Size() const;                //返回当前有效数据大小（不包含文件头）

void Clear();

void Push(const void* data,size_t size);        //追加写入

double GetRatio() const;            //计算​​使用率​​（数据大小/总容量）

bool Empty() const { return Size() == 0; }


private:
struct MmapHeader
{
    static constexpr uint32_t  kMagic = 0xdeadbeef;     //写死一个魔数
    uint32_t magic = kMagic;
    uint32_t size;                                      //有效数据的大小
};

private:

void Reserve_(size_t new_size);                                 //预留指定大小的容量

void EnsureCapacity_(size_t new_size);                          //确保有足够容量（不足时自动扩容）

size_t Capacity_() const noexcept { return capacity_; }         //添加noexcept关键字，承诺不抛出异常，便于编译器优化

bool TryMap_(size_t capacity);                                  //尝试内存映射，要映射的容量大小 (bool非常好，给了我们补救的机会)

void Unmap_();                                                  //解除内存映射            

void Sync_();                                                   //将内存中的数据同步到磁盘文件

bool IsValid_() const;                                          //检查映射是否有效（通过魔数验证）

MmapHeader* Header_() const;                                    //返回文件头指针（确保安全正确的返回）

void Init_();                                                   //初始化新的映射文件（设置初始文件头）

private:
fpath file_path_;               //映射的文件路径
void* handle_;                  //内存映射的句柄/起始地址
size_t capacity_;               //当前映射的总容量

};


}



