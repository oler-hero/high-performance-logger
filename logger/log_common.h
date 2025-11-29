#pragma once

#include <cstring>      // C++ 风格头文件
#include <cstdint>      //核心作用是定义 ​​固定宽度的整数类型​​
#include <memory>
#include <string>       
#include <string_view>  //轻量级、非拥有型的字符串视图​​.(只读)
#include <type_traits>  //提供 ​​编译期的"类型萃取"与特性检查

#include "third_party/fmt/include/fmt/format.h"

#define LOGGER_LEVEL_TRACE 0
#define LOGGER_LEVEL_DEBUG 1
#define LOGGER_LEVEL_INFO 2
#define LOGGER_LEVEL_WARN 3
#define LOGGER_LEVEL_ERROR 4
#define LOGGER_LEVEL_CRITICAL 5
#define LOGGER_LEVEL_OFF 6

namespace logger {

using StringView = std::string_view;        //字符串的只读视图
using MemoryBuf = std::string;              //可变的内存缓冲（名字直译“内存缓冲区”）

//编译期校验占位符和参数是否匹配（数量/类型都查）
//FormatString<Args...> 用在“格式化接口”的第一个形参上，给你“编译期格式串校验”的能力；
template <typename ...Args>
using FormatString = fmt::format_string<Args...>;

//remove_cv消除const和volatile，remove_reference消除引用符号
template <typename T>
using remove_cvref_t = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

//这俩均为“类型萃取（type traits）”
//std::integral_constant<bool,xxx>标准库里“把编译期常量做成类型”
template <typename T,typename Char = char>
struct is_convertible_to_basic_format_string
    : std::integral_constant<bool,
                             std::is_convertible<T, fmt::basic_string_view<Char>>::value ||
                                std::is_same<remove_cvref_t<T>, fmt::runtime_format_string<Char>>::value> {};
//:is_convertible在编译期判断“From 是否能隐式转换为 To”,前者form，后者to。
//remove_cvref_t<T> 去掉 T 的 const/volatile 和引用修饰后，是否正好等于 fmt::runtime_format_string<Char>  表示“这是运行时格式串”的特例（fmt 用它绕过编译期检查）。
//mt:basic_string_view<char>等价于“窄字符"视图（可理解为fmt的string_view）。
//fmt:basic_string_view<wchar_t>是"宽字符"视图。                                

//字面量/const char/std::string/std::string_view 等能隐式构造成 fmt 的字符串视图 → 视为"格式串"
//两个 trait 不冲突：basic = 单字符集判断；any = 合并两字符集判断。
//"能识别"靠的是：第一个参数的类型 Fmt 与 fmt 的字符串视图/运行时格式串是否满足类型关系（is_convertible / is_same），编译期就能算出 true/false，从而选对重载。
template <typename T>
struct is_convertible_to_any_format_string
    : std::integral_constant<bool,
                             is_convertible_to_basic_format_string<T, char>::value ||
                                is_convertible_to_basic_format_string<T, wchar_t>::value> {};




enum class LogLevel {
    kTrace = LOGGER_LEVEL_TRACE,
    kDebug = LOGGER_LEVEL_DEBUG,
    kInfo = LOGGER_LEVEL_INFO,
    kWarn = LOGGER_LEVEL_WARN,
    kError = LOGGER_LEVEL_ERROR,
    kFatal = LOGGER_LEVEL_CRITICAL,
    kOff = LOGGER_LEVEL_OFF
};

struct SourceLocation {

    //允许在编译期常量上下文里使用默认构造（如作为全局/静态对象的常量初始化、作为默认参数）。
    //默认构造本身是“什么都不做”的纯字面初始化，做成 constexpr 没成本，还更泛用。
    constexpr SourceLocation() = default;

    SourceLocation(StringView file_name_in, int32_t line_in, StringView func_name_in)
     :file_name(file_name_in), line(line_in), func_name(func_name_in) {
        if (!file_name_in.empty()) {
            size_t pos = file_name_in.rfind('/');       //先找unix的
            if (pos != StringView::npos) {              //npos,一个超级大的数，表示没找到
                file_name = file_name_in.substr(pos + 1);  //返回"从 pos 到末尾"的视图（零拷贝、不分配）
            } else {
                pos = file_name_in.rfind('\\');  //再找win（重用pos变量）
                if (pos != StringView::npos) {
                    file_name = file_name_in.substr(pos + 1);  //修复：使用 file_name_in
                }
            }
        }
    }




    StringView file_name;
    int32_t line{0};
    StringView func_name;

};


}