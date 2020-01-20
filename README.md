# C++中柯里化(curry)与偏应用(partial)的实现

[TOC]

## 前言

回家的时候在火车上草草看了下《JS函数式编程指南》，对Ramda库中curry和partial的实现略感兴趣，加之不久前，在完成C++期末大作业（[实现一个类似python中的list的C++泛型容器](https://github.com/Light-of-Hers/python-like-list)）的过程中，深入学习了一下C++模板元编程的技术，因此便想用泛型编程技术实现C++中的curry和partial函数。

对于柯里化和偏应用不太了解的，可以参考[@罗宸的这个回答](https://www.zhihu.com/question/30097211/answer/46785556)，在此不多赘述。



## 基本思路

### curry的实现思路

以`int add(int a, int b) {return a + b;}`为例，该函数柯里化后得到的函数对象`c_add`接收一个`int`参数a，返回一个函数对象，该对象保存之前传入的`int`参数a，接收一个`int`参数b，返回`a + b`的值。

这样看来柯里化后的函数对象F必须都保存调用时传入的参数，并且能将当前保存的所有参数传递给调用后返回的函数对象。

可以有两种实现思路：

+ 采用某种泛型数据结构（如`std::tuple`）保存积累至今的参数，并在传入最后一个参数的时候用某种方式一次性将所有参数传递给原函数（如`std::apply`）。这种方法比较容易想到。

+ 还有一种方法参见[何涛的这篇博客](https://sighingnow.github.io/%E7%BC%96%E7%A8%8B%E8%AF%AD%E8%A8%80/cpp_currying_partial_application.html)。其基本思想是保存之前的函数对象，传入最后一个参数后逐层传递参数（最高层N将最后一个参数传递给下一层N-1，N-1将得到的参数和自己保存的参数传递给下一层N-2），在最后一层调用柯里化之前的函数。实现上会更漂亮些，但性能上感觉不如前者。可以用下面的JS伪代码表现：

  ```javascript
  const curry = (f) => {
      return (arg) => {
          return curry(partial_first_arg(f, arg));
      }
  }
  ```



### partial的实现思路

偏应用的实现就比较直观，其实现思路也有两种：

+ 比较直观的方法：保存传入的参数，在被调用时连同传入的参数一起传给原函数。

+ 也是[何涛博客](https://sighingnow.github.io/%E7%BC%96%E7%A8%8B%E8%AF%AD%E8%A8%80/cpp_currying_partial_application.html)中提到的方法，基本思想可以用下面JS伪码表述：

  ```javascript
  const partial = (f, arg, ...args) => {
      return partial(partial_first_arg(f, arg), ...args);
  }
  ```



## 实现坑点

关于curry/partial的C++实现，有不少先例，如[何涛的一篇博客](https://sighingnow.github.io/%E7%BC%96%E7%A8%8B%E8%AF%AD%E8%A8%80/cpp_currying_partial_application.html)、[@Khellendros的一篇文章](https://zhuanlan.zhihu.com/p/52715274)、[stackoverflow上的一个问题](https://stackoverflow.com/questions/152005/how-can-currying-be-done-in-c)等，但多多少少有一些难以忽视的漏洞，包括但不限于：



1. 采用右值引用结合`std::forward`来转发函数参数，如Khellendros的curry实现：

```C++
auto operator()(A &&... args) const {
    auto cache2 = std::tuple_cat(_cache, std::forward_as_tuple(args...));
    return CurriedFunction<F, i - 1, decltype(cache2)>(_fn, std::move(cache2));
}
```

这样会将所有传入的左值引用都保存为引用，而不是根据原函数的参数类型保存为值/引用，在许多情况下会导致悬垂引用问题。如下面的例子：

```C++
int add(int a, int b) {
    return a + b;
}
auto inc(int x) {
    return Reimuda::curry<2>(add)(x);
}
int main() {
    auto f1 = inc(1);
    auto f2 = inc(2);
    auto f3 = inc(3);
    auto f4 = inc(4);
    auto f5 = inc(5);
    std::cout << f1(1) << std::endl;
}
```

会输出奇怪的数（反正不是2），因为`inc`中的`curry(add)`接收`x`后保存的是`x`的引用而不是值，之后对`inc`的调用会覆写原引用指向的栈上的位置。



2. 采用引用保存原函数。和前例一样也会出现悬垂引用问题。一般对于函数对象而言保存值是最好的，如果需要保存引用可以用`std::ref`显式包装。



3. 对于函数和保存的参数的可拷贝性缺乏考量。



4. 没有按原函数的参数类型要求严格地按值/引用保存传入的参数。这点是1.的延伸。

......



上述问题主要都是C++的内存管理模式导致的。和ML、Lisp等大多数带函数式编程特性的语言不同，C++没有GC，这就导致了实现curry&partial的过程中，在涉及值/引用和拷贝/移动时需要更细致的考察（用Rust的话编译器会帮你考察……）。



## 实现细节

### 函数签名的萃取

前面的部分说过，想要在C++中实现一个行为较为正确的curry&partial，必须显式地解析参数类型，根据参数类型来决定保存传入的参数的值还是引用。因此需要一些TMP技巧来萃取函数对象的签名。这部分内容可以参见[这篇文章](https://zhuanlan.zhihu.com/p/102240099)，这里仅给出代码：

```C++
namespace __detail {

template<typename R, typename ...As>
struct __function_traits_base {
    using function_type = std::function<R(As...)>;

    using result_type = R;

    using argument_types = std::tuple<As...>;
};

template<typename F>
struct __function_traits;
template<typename F>
struct __function_traits<std::reference_wrapper<F>> : public __function_traits<F> {};
template<typename R, typename ...As>
struct __function_traits<R(*)(As...)> : public __function_traits_base<R, As...> {};
template<typename R, typename C, typename ...As>
struct __function_traits<R(C::*)(As...)> : public __function_traits_base<R, As...> {};
template<typename R, typename C, typename ...As>
struct __function_traits<R(C::*)(As...) const> : public __function_traits_base<R, As...> {};
template<typename F>
struct __function_traits : public __function_traits<decltype(&F::operator())> {};

}

namespace fp {

template<typename F>
struct function_traits : public __detail::__function_traits<std::decay_t<F>> {};

}
```



### curry的实现

采用第一种思路实现。



首先定义一个可以缓存参数的cacher作为curry的返回值。模板中的`TA`表示"Tuple of Args"，即保存的参数元组的类型，`A`和`As`表示之后还剩下的参数类型。cacher接收一个参数，用其扩展`cached_args`，将扩展后的`cached_args`传给一个新的cacher，并返回新cacher。

其中注意`cached_args`和`f`可能不可拷贝，因此用`__copy_or_move`包装，视情况将其拷贝/移动给新的cacher。

```C++
template<typename F, typename T1, typename T2>
class __curry_cacher;
template<typename F, typename TA, typename A, typename ...As>
class __curry_cacher<F, TA, std::tuple<A, As...>> {
    F f;
    TA cached_args;
public:
    __curry_cacher(F f, TA args) : f(std::move(f)), cached_args(std::move(args)) {}
    auto operator()(A arg) {
        auto new_cached_args = std::tuple_cat(
                __copy_or_move(cached_args),
                std::tuple<A>(std::forward<A>(arg)));
        return __curry_cacher<F,
                decltype(new_cached_args),
                std::tuple<As...>>(__copy_or_move(f), std::move(new_cached_args));
    }
}
```

其中`__copy_or_move`的实现如下（虽然比较trivial，但还是贴出来吧）：

```C++
template<typename T>
auto __copy_or_move(const T &t) -> T {
    if constexpr (std::is_copy_constructible_v<T>) {
        return t;
    } else {
        return std::move(const_cast<T &>(t));
    }
}
```



当剩余的参数只有一个的时候，调用cacher将直接调用保存的原函数：

```C++
template<typename F, typename TA, typename A>
class __curry_cacher<F, TA, std::tuple<A>> {
    F f;
    TA cached_args;
public:
    __curry_cacher(F f, TA args) : f(std::move(f)), cached_args(std::move(args)) {}
    auto operator()(A arg) {
        return std::apply(f, std::tuple_cat(
                __copy_or_move(cached_args),
                std::tuple<A>(std::forward<A>(arg))));
    }
};
```



实现完cacher后，就可以实现curry了。先将参数类型的元组萃取出来，之后返回一个没有缓存参数的cacher。

```C++
template<typename F>
auto curry(F f) {
    using arg_types = typename function_traits<F>::argument_types;
    if constexpr (std::tuple_size_v<arg_types> < 2) {
        return f;
    } else {
        return __detail::__curry_cacher<F, std::tuple<>, arg_types>
                (std::move(f), std::tuple<>());
    }
}
```



### partial的实现

也采用第一种思路实现partial。



先定义一个cacher来缓存参数：

```C++
template<typename F, typename T1, typename T2>
class __partial_cacher;
template<typename F, typename TA, typename ...As>
class __partial_cacher<F, TA, std::tuple<As...>> {
    F f;
    TA cached_args;
public:
    __partial_cacher(F f, TA args) : f(std::move(f)), cached_args(std::move(args)) {}
    auto operator()(As... args) {
        return std::apply(f, std::tuple_cat(
                __copy_or_move(cached_args),
                std::tuple<As...>(std::forward<As>(args)...)));
    }
};
```



之后实现partial。同样是先萃取函数签名，在返回一个缓存着接收到的参数的cacher。注意cacher模板的第三个参数是接下来还要接收的函数参数类型，因此需要用函数签名中的参数类型刨去前面已经接收的参数类型：

```C++
template<typename F, typename ...As>
auto partial(F f, As &&...args) {
    using arg_types = typename function_traits<F>::argument_types;
    static_assert(sizeof...(As) <= std::tuple_size_v<arg_types>, "Too many arguments");
    if constexpr (sizeof...(As) == 0) {
        return f;
    } else if constexpr (sizeof...(As) == std::tuple_size_v<arg_types>) {
        return f(std::forward<As>(args)...);
    } else {
        return __detail::__partial_cacher<F, std::tuple<As...>, __detail::__tuple_drop_n_t<sizeof...(As), arg_types>>
                (std::move(f), std::tuple<As...>(std::forward<As>(args)...));
    }
}
```

其中`__tuple_drop_n_t<N, T>`用于丢弃元组`T`的前`N`个类型，实现如下：

```C++
template<std::size_t I, typename T, typename = void>
struct __tuple_drop_n;
template<std::size_t I, typename T>
using __tuple_drop_n_t = typename __tuple_drop_n<I, T>::type;
template<typename ...Ts>
struct __tuple_drop_n<0, std::tuple<Ts...>> {
    using type = std::tuple<Ts...>;
};
template<std::size_t I, typename T, typename ...Ts>
struct __tuple_drop_n<I, std::tuple<T, Ts...>, std::enable_if_t<(I > 0)>> {
    using type = __tuple_drop_n_t<I - 1, std::tuple<Ts...>>;
};
```



## 简单测试

### curry的测试

#### call-by-value

```C++
    auto add = [](int a, int b, int c, int d) {
        return a + b + c + d;
    };
    auto c_add = curry(add);
    std::cout << c_add(1)(2)(3)(4) << std::endl; // 10
    auto c_add_1 = c_add(1);
    std::cout << c_add_1(2)(3)(4) << std::endl; // 10
```

#### call-by-reference

```C++
    auto genso_concat = [](std::string &s) {
        return curry([&](std::string &a, const std::string &b) {
            auto tmp = a;
            a += " " + b + " " + s;
            s += " " + b + " " + tmp;
        });
    };
    auto s1 = std::string("Reimu"), s2 = std::string("Marisa");
    auto f = genso_concat(s1);
    auto ff = f(s2);
    ff("love");
    std::cout << s1 << std::endl; // Reimu love Marisa
    std::cout << s2 << std::endl; // Marisa love Reimu
```

#### closure?

```C++
    auto greater_than = [](int x) {
        return curry([](int a, int b) { return a < b; })(x);
    };
    auto gt_0 = greater_than(0);
    auto gt_1 = greater_than(1);
    auto gt_2 = greater_than(2);
    auto gt_3 = greater_than(3);
    std::cout << std::boolalpha;
    std::cout << gt_0(-1) << std::endl; // false
    std::cout << gt_0(0) << std::endl; // false
    std::cout << gt_0(1) << std::endl; // true
```

#### non-copyable-function

```c++
class UniF {
    std::unique_ptr<int> uip;
public:
    explicit UniF(int x) : uip(new int{x}) {}
    int operator()(int &a, int b) {
        return a = *uip += b;
    }
};
{
    UniF uf{1};
    int x = 0;
    auto f = curry(std::move(uf));
    auto x_f = std::move(f(x));
    std::cout << x_f(1) << std::endl; // 2
    std::cout << x_f(1) << std::endl; // 3
    std::cout << x_f(1) << std::endl; // 4
    std::cout << x << std::endl; // 4
}
{
    UniF uf{1};
    int x = 0;
    auto f = curry(std::ref(uf));
    std::cout << f(x)(1) << std::endl; // 2
    std::cout << x << std::endl; // 2
    auto x_f = f(x);
    std::cout << x_f(1) << std::endl; // 3
    std::cout << x_f(1) << std::endl; // 4
    std::cout << x << std::endl; // 4
}
```



### partial的测试

#### call-by-value

```C++
    auto gt_0 = partial(std::less<int>{}, 0);
    std::cout << std::boolalpha;
    std::cout << gt_0(-1) << std::endl; // false
    std::cout << gt_0(0) << std::endl; // false
    std::cout << gt_0(1) << std::endl; // true
```

#### call-by-reference

```C++
    auto pair_assign = [](int &a, int &b, int aa, int bb) -> void { a = aa, b = bb; };
    int a = 0, b = 0;
    auto assign_a_b = partial(pair_assign, a, b);
    assign_a_b(1, 2);
    std::cout << a << ", " << b << std::endl; // 1, 2
```

#### non-copyable-function

```C++
    std::unique_ptr<int> uip{new int{0}};
    auto uf = [p = std::move(uip)](int a, int b) {
        return *p += a + b;
    };
    {
        auto f = partial(std::ref(uf), 1);
        std::cout << f(1) << std::endl; // 2
        std::cout << f(1) << std::endl; // 4
        std::cout << f(1) << std::endl; // 6
    }
    {
        auto f = partial(std::move(uf), 1);
        std::cout << f(1) << std::endl; // 8
        std::cout << f(1) << std::endl; // 10
        std::cout << f(1) << std::endl; // 12
    }
```



## 使用注意

+ 传给curry&partial的必须是类型确定的函数，也就是说函数模板和重载的函数（含有默认形参的函数也算）不能直接传入。对于函数模板，需要实例化后传入；对于重载的函数，需要显式转换到确定的类型才能传入。
+ curry&partial默认传入函数的拷贝，如果想传入函数引用可以用`std::ref`/`std::cref`进行包装后将引用间接传入。如果传入的函数对象是不可拷贝的，可以选择用`std::ref`间接传引用或者用`std::move`转让所有权。
+ 需要C++17标准支持（主要是`constexpr if`特性，毕竟多分支模板匹配写起来还是蛮令人不爽的……）。



## 后记

自己在PKU-CECA里搞的东西基本和PL或C++没有什么关系（硬要扯上关系的话，TVM的Relay的研究和PL有点关系？或者写CUDA之类的和C++有点关系？），所以这两天搞这种无用的玩意纯属摸鱼行为……不过，虽然无用，但造轮子还是蛮爽的。

附上代码链接：https://github.com/Light-of-Hers/Cpp-curry-partial-and-other-FP-combinators



*本文纯原创，如需转载请标明出处*

