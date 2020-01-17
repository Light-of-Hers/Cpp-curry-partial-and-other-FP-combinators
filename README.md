# curry、partial及部分常用FP组合子的C++实现

[TOC]

## 前言

前阵子（指一年半前）稍微了解了一下Haskell，虽然没有深入学习，但对其中的柯里化印象深刻。不久前（指一年前）较为系统地（稍微）学习了一下FP（指阅读SICP和EOPL），对FP的优雅有了更深入的认识。前不久（指前不久）在完成C++期末大作业（[实现一个类似python中的list的C++泛型容器](https://github.com/Light-of-Hers/python-like-list)）的时候，对TMP有了较为深入的研究，于是便想试着用C++实现一下curry、partial等函数。实现出的效果如下：

```C++
class UniF {
    std::unique_ptr<int> uip;
public:
    explicit UniF(int x) : uip(new int{x}) {}
    int operator()(int &a, int b) {
        return a = *uip += b;
    }
};

int add(int a, int b) {
    return a + b;
}
int main() {
    using namespace crz::ft;
    {
        auto c_add = curry(add);
        std::cout << c_add(1)(2) << std::endl; // 3
        auto add_1 = c_add(1);
        std::cout << add_1(1) << std::endl; // 2
    }
    {
        int n = 0;
        auto self_add = curry([&](int &a, int b) { return a += b + n; });
        int x = 0;
        std::cout << self_add(x)(1) << std::endl; // 1
        std::cout << x << std::endl; // 1
        n = 1;
        auto x_add = self_add(x);
        std::cout << x_add(1) << std::endl; // 3
        std::cout << x << std::endl; // 3
    }
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
        auto pair_assign = [](int &a, int &b, int aa, int bb) -> void { a = aa, b = bb; };
        int a = 0, b = 0;
        auto assign_a_b = partial(pair_assign, a, b);
        assign_a_b(1, 2);
        std::cout << a << ", " << b << std::endl; // 1, 2
    }
}
```

实现的[源码链接](https://github.com/Light-of-Hers/Cpp-curry-partial-and-other-FP-combinators)



## 前置准备

curry和partial涉及到对函数参数类型的解析（curry/partial出的函数会暂存传入的参数，因此有必要显式地解析各个参数的类型，以选择适当的方式（传值/传引用）来保存参数），要通过一些手段萃取函数签名的信息。这部分内容可以参见[这篇文章](https://zhuanlan.zhihu.com/p/102240099)以及[stackoverflow上的一个问题](https://stackoverflow.com/questions/6512019/can-we-get-the-type-of-a-lambda-argument)，这里仅给出代码：

```C++
namespace detail {

template<typename R, typename ...As>
struct __function_traits_base {
    using function_type = std::function<R(As...)>;

    using result_type = R;

    using argument_types = std::tuple<As...>;
};

template<typename F>
struct __function_traits : public __function_traits<decltype(&F::operator())> {};
template<typename R, typename ...As>
struct __function_traits<R(*)(As...)> : public __function_traits_base<R, As...> {};
template<typename R, typename C, typename ...As>
struct __function_traits<R(C::*)(As...)> : public __function_traits_base<R, As...> {};
template<typename R, typename C, typename ...As>
struct __function_traits<R(C::*)(As...) const> : public __function_traits_base<R, As...> {};

}

namespace ft {

template<typename F>
struct function_traits
        : public detail::__function_traits<std::decay_t<std::remove_reference_t<F>>> {
};

}
```

这样将函数类型传进`function_traits`的模板，就可以获取函数的签名信息了。



## curry的实现

### 基本思路

以`int add(int a, int b) {return a + b;}`为例，该函数柯里化后得到的函数对象`c_add`接收一个`int`参数a，返回一个函数对象，该对象保存之前传入的`int`参数a，接收一个`int`参数b，返回`a + b`的值。

这样看来柯里化后的函数对象F必须都保存调用时传入的参数，并且能将当前保存的所有参数传递给调用后返回的函数对象F1。

一种思路是采用某种递归结构保存各个参数，在传入最后一个参数后将所有参数一次性地传给柯里化之前的函数。这种方法实现起来稍微有点麻烦。

另一种思路是保存之前的函数对象，传入最后一个参数后逐层传递参数（最高层N将最后一个参数传递给下一层N-1，N-1将得到的参数和自己保存的参数传递给下一层N-2），在最后一层调用柯里化之前的函数。该思路和前一种本质上类似（因为前一种虽然是将所有参数直接传给柯里化之前的函数，但仍需逐层展开保存的参数）。

第二种思路可以变换一下。借助partial，curry实际上可以实现为（用JS代码来表达一下意思）：

```javascript
const curry = (f) => {
    return (arg) => {
        return curry(single_partial(f, arg));
    }
}
```

注意这里的用到的partial实际上只应用第一个参数，因此在这时并不需要实现功能完整的partial（因此命名为single_partial）。



### 实现方式

首先定义一个`single_partialer`，获得对一个函数应用第一个参数后得到的函数对象：

```C++
template<typename R, typename A, typename ...As>
class __single_partialer {
    std::function<R(A, As...)> f;
    A arg;
public:
    __single_partialer(std::function<R(A, As...)> f, A arg)
            : f(std::move(f)), arg(std::forward<A>(arg)) {}
    auto operator()(As... args) {
        return f(std::forward<A>(arg), std::forward<As>(args)...);
    }
};
```



接着按照之前所说的思想，先针对`std::function`实现curry：

```c++
template<typename R, typename A = void>
auto __curry(std::function<R(A)> f) {
    return f;
}

template<typename R, typename A, typename ...As>
auto __curry(std::function<R(A, As...)> f) {
    return [f = std::move(f)](A arg) {
        auto partialer = __single_partialer<R, A, As...>(std::move(f), std::forward<A>(arg));
        __functor_copy_wrapper<decltype(partialer), std::function<R(As...)>>
                wrapper(std::move(partialer));
        std::function<R(As...)> rest = wrapper;
        return __curry(std::move(rest));
    };
}
```

这里`partialer`也可用一个lambda表达式实现，不过捕获`arg`时会出现些问题（特别是函数参数为引用，即`A`为左值/右值引用类型时），有兴趣的可以自己试试。

注意`std::function`只接收可拷贝的函数对象，因此为了应对不可拷贝的函数对象（保存有不可拷贝的值，或者保存有右值引用），用一个`functor_copy_wrapper`包装函数对象，对不可拷贝的对象重写拷贝构造函数为移动语义。`functor_copy_wrapper`实现如下：

```C++
template<typename, typename, typename = void>
class __functor_copy_wrapper;

template<typename F, typename R, typename ...As>
class __functor_copy_wrapper<F, std::function<R(As...)>,
        std::enable_if_t<!std::is_copy_constructible_v<F>>> {
    using self = __functor_copy_wrapper;

    F f;
public:
    explicit __functor_copy_wrapper(F f) : f(std::move(f)) {}
    __functor_copy_wrapper(const self &rhs) : f(std::move(const_cast<self &>(rhs).f)) {}
    __functor_copy_wrapper(self &&rhs) noexcept : f(std::move(rhs.f)) {}

    auto operator()(As ...args) {
        return f(std::forward<As>(args)...);
    }
};

template<typename F, typename R, typename ...As>
class __functor_copy_wrapper<F, std::function<R(As...)>,
        std::enable_if_t<std::is_copy_constructible_v<F>>> {
    F f;
public:
    explicit __functor_copy_wrapper(F f) : f(std::move(f)) {}

    auto operator()(As ...args) {
        return f(std::forward<As>(args)...);
    }
};
```



接着再针对一般的函数类型实现curry：

```C++
template<typename F>
auto curry(F f) {
    using func_type = typename function_traits<F>::function_type;
    detail::__functor_copy_wrapper<F, func_type> wrapper(std::move(f));
    func_type functor = wrapper;
    return detail::__curry(std::move(functor));
}
```

其中利用了之前提到的`function_traits`萃取`F`对应的`std::function`的类型。



### 使用注意

+ 传入的必须是类型确定的函数，也就是说函数模板和重载的函数不能直接传入。对于函数模板，需要实例化成模板函数后传入；对于重载的函数，需要显式转换到特定的类型才能传入。

+ 当前实现的curry只支持对原函数的拷贝，不支持对原函数的引用。主要是考虑到相当一部分传递给curry的函数会是局部的对象（curry化过程中生成的中间函数也都是局部的对象），保存引用会导致悬空指针的问题。对于不支持拷贝的函数对象，会采用移动语义传递函数实体，这就导致：

  + 将函数传入curry时需要用`std::move`将所有权转让给curry。
  + curry后的函数调用后会将原函数的所有权转让给返回的函数对象，如果不将其返回值捕获（如前言的示例中的`auto x_f = std::move(f(x));`），则其原函数实体会消亡。

  今后可能会改进curry的实现让其支持对原函数的引用（比如用`std::ref`显式传入函数的引用）。



## partial的实现

### 基本思路

partial实现起来就没那么绕了，思路可以参见伪代码：

```javascript
const partial = (f, arg, ...args) => {
    return partial(single_partial(f, arg), args...);
}
```



### 实现方法

实现的代码结构和curry类似，直接放出代码：

```C++
template<typename R, typename ...Ps>
auto __partial(std::function<R(Ps...)> f) {
    return f;
}

template<typename R, typename P, typename ...Ps, typename A, typename ...As>
auto __partial(std::function<R(P, Ps...)> f, A &&arg, As &&...args) {
    auto partialer = __single_partialer<R, P, Ps...>(std::move(f), std::forward<A>(arg));
    __functor_copy_wrapper<decltype(partialer), std::function<R(Ps...)>>
            wrapper(std::move(partialer));
    std::function<R(Ps...)> rest = wrapper;
    return __partial(std::move(rest), std::forward<As>(args)...);
}
```

```C++
template<typename F, typename ...As>
auto partial(F f, As &&...args) {
    using func_type = typename function_traits<F>::function_type;
    detail::__functor_copy_wrapper<F, func_type> wrapper(std::move(f));
    func_type functor = wrapper;
    return detail::__partial(std::move(functor), std::forward<As>(args)...);
}
```



### 使用注意

和curry类似，不再赘述。



## 其他部分常用FP组合子的实现

写完curry和partial就顺便实现了compose、pipe等部分FP组合子，仅在此贴一下实现（用到了C++14的泛型lambda表达式）：

```C++

template<typename F>
auto pipe(F f) {
    return f;
}
template<typename F, typename ...Fs>
auto pipe(F f, Fs ...fs) {
    return [=, f = std::move(f)](auto &&...args) {
        return pipe(fs...)(f(std::forward<decltype(args)>(args)...));
    };
}

template<typename F>
auto compose(F f) {
    return f;
}
template<typename F, typename ...Fs>
auto compose(F f, Fs ...fs) {
    return [=, f = std::move(f)](auto &&...args) {
        return f(compose(fs...)(std::forward<decltype(args)>(args)...));
    };
}

template<typename F1, typename F2, typename J>
auto fork_join(F1 f1, F2 f2, J join) {
    return [f1 = std::move(f1), f2 = std::move(f2), join = std::move(join)](auto &&...args) {
        return join(f1(std::forward<decltype(args)>(args)...),
                    f2(std::forward<decltype(args)>(args)...));
    };
}

template<typename T>
auto identity(T &&t) {
    return std::forward<T>(t);
}

template<typename T>
auto seq(T &&t) {
    return std::forward<T>(t);
}
template<typename T, typename F, typename ...Fs>
auto seq(T &&t, F f, Fs ...fs) {
    f(std::forward<T>(t));
    return seq(std::forward<T>(t), fs...);
}
```





 *本文纯原创，如需转载请标明出处*
