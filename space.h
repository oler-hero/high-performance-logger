#pragma once

#include <ratio>
#include<type_traits>
#include <cstddef>

namespace logger {

template<typename _Rep, typename _Capacity = std::ratio<1>>
class space;

template<typename _ToSpace, typename _Rep, typename _Capacity> 
constexpr _ToSpace space_cast(const space<_Rep, _Capacity>& __d) {
    auto __r = __d.count() *                              //这里有个原理，直接用分式理解就行，ratiodivide前一个是被除数
            std::ratio_divide<_Capacity, _ToSpace::period>::num /
            std::ratio_divide<_Capacity, _ToSpace::period>::den;

    return _ToSpace(__r);           //返回的是类型转换之后的比例，先计算和外面类型的比例获取外面类型的值大小，结合外面《》里的数据类型，就能够做到类型转换
}

template<typename R1, typename R2>
struct ratio_less {         //为什么用struct，因为他能写常量表达式，能在编译器就完成计算
    static constexpr bool value = R1::num * R2::den < R2::num * R1::den;

};

template<typename _Rep1, typename _Capacity1, typename _Rep2, typename _Capacity2>
struct higher_pression_duration {
    using type = typename std::conditional<ratio_less<_Capacity1, _Capacity2>::value,      //判断逻辑，逻辑为真，则左边
                                            space<_Rep1, _Capacity1>,               //逻辑为真的情况，左边精度必须更高。
                                            space<_Rep2, _Capacity2>>::type;
};

template<typename _Rep, typename _Capacity>
class space {
 public:
    using rep = _Rep;
    using period = _Capacity;

    constexpr space(): _rep() {}

    template<typename _Rep2>
    explicit constexpr space (const _Rep2& __r) : _rep(__r) {}

    template<typename _Rep2, typename _Capacity2>
    constexpr space(const space<_Rep2,_Capacity2>& __d) : _rep(space_cast<space<_Rep, _Capacity>>(__d).count()) {}     //将--d转换成当前space类型再获取count,就构造了一个新对象

    constexpr _Rep count() const { return _rep; }       //常量表达式代表编译期能够求值，而const确保函数不修改对象状态，可在const对象上调用

    constexpr space operator+() const {
        return *this;
    }

    constexpr space operator-() const {
        return space(-count());                     //重新构造了一个rep为-count的space对象再返回
    }

    space& operator++() {
        ++ _rep;
        return *this;
    }

    space operator++(int) {             //int是一标识，用来区分前置++和后置++
        space __tmp(*this);             //*this是当前对象的引用（space&类型）
        ++ _rep;
        return __tmp;
    }

    space& operator--() {
        -- _rep;
        return *this;
    }

    space operator--(int) {             //int是一标识，用来区分前置--和后置--
        space __tmp(*this);             //*this是当前对象的引用（space&类型）
        -- _rep;
        return __tmp;
    }

    //因为一般用不到这些，所以直接锁定同类型+=这些就行，+-再拓宽

    space operator+=(const space& __d) {
        _rep += __d.count();
        return *this;
    }

    space& operator-=(const space& __d) {
        _rep -= __d.count();
        return *this;
    }

    space& operator*=(const _Rep& __s) {
        _rep *= __s;
        return *this;
    }

    space& operator/=(const _Rep& __s) {
        _rep /= __s;
        return *this;
    }

    space& operator%=(const _Rep& __s) {
        _rep %= __s;
        return *this;
    }

 private:
 _Rep _rep;              //因为类似于标准库的实现，所以这样命名，rep是值

};

//一个不同类型进行计算的符号重载（自动转换高精度类型）
template<typename _Rep1, typename _Capacity1, typename _Rep2, typename _Capacity2>
constexpr auto operator+(const space<_Rep1, _Capacity1> & __lhs, const space<_Rep2, _Capacity2> & __rhs) {
    using _Dur = typename higher_pression_duration<_Rep1, _Capacity1, _Rep2, _Capacity2>::type;     //是在计算一个高精度的类型出来
    return _Dur(space_cast<_Dur>(__lhs).count() + space_cast<_Dur>(__rhs).count());                 //返回这种类型的东西
}
//加入const的原因是确保能够接收临时量和常量
template<typename _Rep1, typename _Capacity1, typename _Rep2, typename _Capacity2>
constexpr auto operator-(const space<_Rep1, _Capacity1> & __lhs, const space<_Rep2, _Capacity2> & __rhs) {
    using _Dur = typename higher_pression_duration<_Rep1, _Capacity1, _Rep2, _Capacity2>::type;
    return _Dur(space_cast<_Dur>(__lhs).count() - space_cast<_Dur>(__rhs).count());
}

using kilo = std::ratio<1024LL, 1>;                         //1024
using mega = std::ratio<1024LL * 1024, 1>;                  //1024 * 1024
using giga = std::ratio<1024LL * 1024 * 1024, 1>;           //1024 * 1024 * 1024
using tera = std::ratio<1024LL * 1024 * 1024 * 1024, 1>;    //1024 * 1024 * 1024 * 1024

using bytes = space<size_t>;            //size_t本身没有"具体数值"，但定义了数值的"容器"
using kilobytes = space<size_t, kilo>;
using megabytes = space<size_t, mega>;
using gigabytes = space<size_t, giga>;
using terabytes = space<size_t, tera>;
}


