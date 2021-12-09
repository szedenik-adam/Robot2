#pragma once

#include <iostream>
/*
void prefix() { std::cout << "prefix\n"; }
void suffix() { std::cout << "suffix\n"; }

template<class T>
class Call_proxy {
    T* p;
public:
    Call_proxy(T* pp) :p(pp) {}
    ~Call_proxy() { suffix(); }
    T* operator->() { return p; }
};

template<class T> class Wrap {
    T* p;
public:
    Wrap(T* pp) :p(pp) {}
    Call_proxy<T> operator->() { prefix(); return Call_proxy<T>(p); }
};

class Example {
public:
    Example() { std::cout << "Example ctor\n"; }
    ~Example() { std::cout << "Example dtor\n"; }
    void TestFunc() { std::cout << "Example::TestFunc\n"; }
    int TestFunc2(int param) { std::cout << "Example::TestFunc2\n"; return 1; }
};

template <typename Interface>
class Proxy
{
    Interface* i;
public:
    Proxy(Interface* pp) :i(pp) {}

    template <typename F, typename... Params>
    auto operator()(F f, Params... parameters)
        -> decltype((i->*f)(parameters...))
    {
        return (i->*f)(parameters...);
    }
};

void ProxyTest()
{
    Wrap<Example> example(new Example);
    example->TestFunc();

    Proxy<Example> example2(new Example);
    example2(&Example::TestFunc2, 1);
}*/

// ----------------------------------------------------------------

#include <map>
#include <list>
#include <tuple>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <algorithm>

template<size_t N>
struct StringLiteral {
    constexpr StringLiteral(const char(&str)[N]) {
        std::copy_n(str, N, value);
    }

    char value[N];
};

template <StringLiteral title>
class Benchmark;

class BenchmarkCollector
{
    inline static std::map <const char*, std::tuple<std::chrono::microseconds&, size_t&>> benchmarks;
public:
    static void Print()
    {
        for (auto const& [key, val] : benchmarks)
        {
            std::cout << key << ":"
                << std::setfill(' ') << std::setw( 5) << std::get<1>(val) << "x "
                << std::setfill(' ') << std::setw(10) << std::get<0>(val).count() << " us\n";
        }
    }

    static void Add(char const* title, std::chrono::microseconds& totalTimeRef, size_t& totalCallsRef)
    {
        std::pair<decltype(std::begin(benchmarks)), bool> empRes = benchmarks.try_emplace(title, totalTimeRef, totalCallsRef);
        if (!empRes.second) {
            printf("Warning: Benchmark \"%s\" is already in the map!", title);
        }
    }
};

template <StringLiteral title>
class Benchmark {
    static std::chrono::microseconds totalTime;
    static size_t totalCalls;
    std::chrono::steady_clock::time_point start;
public:
    Benchmark() : start(std::chrono::high_resolution_clock::now())
    {
        if (!totalCalls++) {  BenchmarkCollector::Add(title.value, totalTime, totalCalls); }
    }
    ~Benchmark()
    {
        std::chrono::steady_clock::time_point end(std::chrono::high_resolution_clock::now());
        std::chrono::microseconds time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        totalTime += time;
    }

    const char* GetString() const { return title.value; }
};

template <StringLiteral title>
std::chrono::microseconds Benchmark<title>::totalTime;
template <StringLiteral title>
size_t Benchmark<title>::totalCalls = 0;


static void BenchmarkTest()
{
    //static const char teststr[] = "Hello world!";
    //Benchmark<teststr> b;
    Benchmark<"Hello world!"> b;
}

// Benchmark with subtypes

template <StringLiteral title, int subtype, const char* typeToStringFunc(int)>
class BenchmarkT;

class BenchmarkTCollector
{
    inline static std::vector<std::tuple<const char*, int, const char*(*)(int), std::reference_wrapper<std::chrono::microseconds>, std::reference_wrapper<size_t>>> benchmarks;
public:
    enum class SortBy {Name, Time, Count};

    static void Print(SortBy sortMode = SortBy::Time)
    {
        // Sort result.
        std::sort(benchmarks.begin(), benchmarks.end(), [sortMode](const auto& a, const auto& b)->bool {
            if (sortMode == SortBy::Name) {
                int titleCmp = strcmp(std::get<0>(a), std::get<0>(b));
                if (titleCmp!=0) return titleCmp > 0;
                const char* (*typeToStringFunc)(int) = std::get<2>(a); // the Title is same so it is safe to assume that the typeToStringFunc will also be the same.
                if (typeToStringFunc) {
                    int typeStrCmp = strcmp(typeToStringFunc(std::get<1>(a)), typeToStringFunc(std::get<1>(b)));  // Compare subtypes text.
                    return typeStrCmp > 0;
                }
                return std::get<1>(a) > std::get<1>(b); // Compare subtypes.
            }
            if (sortMode == SortBy::Time || sortMode == SortBy::Count && std::get<4>(a) == std::get<4>(b)) {
                return std::get<3>(a).get().count() > std::get<3>(b).get().count(); // Compare times.
            }
            return std::get<4>(a).get() > std::get<4>(b).get(); // Compare call count.
            });

        // Calculate padding based on the longest entry's title's length.
        int longestTitleLen = 15;
        for (const auto& elem : benchmarks)
        {
            const char* title = std::get<0>(elem);
            int subtype = std::get<1>(elem);
            const char* (*typeToStringFunc)(int) = std::get<2>(elem);

            int titleLen = strlen(title);
            if (typeToStringFunc) {
                const char* typeStr = typeToStringFunc(subtype);
                titleLen += 1 + strlen(typeStr);
            }
            else if (subtype) {
                titleLen += 1 + log10(subtype) + 1;
            }

            longestTitleLen = std::max(longestTitleLen, titleLen);
        }
        // Write each entry.
        for (const auto& elem : benchmarks)
        {
            const char* title = std::get<0>(elem);
            int subtype = std::get<1>(elem);
            const char* (*typeToStringFunc)(int) = std::get<2>(elem);
            std::chrono::microseconds& totalTime = std::get<3>(elem).get();
            size_t& totalCalls = std::get<4>(elem).get();

            if (typeToStringFunc) {
                const char* typeStr = typeToStringFunc(subtype);
                int titleLen = 1 + strlen(typeStr);
                std::cout << std::setfill(' ') << std::setw(longestTitleLen - titleLen) << title << '-' << typeStr;
            }
            else if (subtype) {
                int titleLen = 1 + log10(subtype) + 1;
                std::cout << std::setfill(' ') << std::setw(longestTitleLen - titleLen) << title << '.' << subtype;
            }
            else {
                std::cout << std::setfill(' ') << std::setw(longestTitleLen) << title;
            }
            std::cout << ":" << std::setfill(' ') << std::setw(5) << totalCalls << "x ";

            long long ticks = totalTime.count(); const char* unit = "u"; int divs = 0; int fraction = 0;
            if (ticks > 1000) { fraction = ticks % 1000; ticks /= 1000; divs++; unit = "m"; }
            if (ticks > 1000) { fraction = ticks % 1000; ticks /= 1000; divs++; unit = ""; }
            std::cout << std::setfill(' ') << std::setw(10-divs*4) << ticks;
            if (divs) { std::cout << '.' << std::setfill('0') << std::setw(3)<< fraction; }
            std::cout << ' '<< unit << "s\n";
        }
    }
    static std::map<std::string, std::tuple<long long, size_t>> Results()
    {
        std::map<std::string, std::tuple<long long, size_t>> result;
        for (auto const& elem : benchmarks)
        {
            const char* title = std::get<0>(elem);
            int subtype = std::get<1>(elem);
            const char* (*typeToStringFunc)(int) = std::get<2>(elem);
            std::chrono::microseconds& totalTime = std::get<3>(elem).get();
            size_t& totalCalls = std::get<4>(elem).get();

            std::stringstream titleStr;
            titleStr << title;

            if (typeToStringFunc) {
                const char* typeStr = typeToStringFunc(subtype);
                int titleLen = 1 + strlen(typeStr);
                titleStr << '-' << typeStr;
            }
            else if (subtype) {
                int titleLen = 1 + log10(subtype) + 1;
                titleStr << '.' << subtype;
            }
            result.emplace(std::make_pair<std::string, std::tuple<long long, size_t>>(titleStr.str(), { totalTime.count(), totalCalls }));
        }
        return result;
    }

    static void Add(char const* title, int subtype, const char* typeToStringFunc(int), std::chrono::microseconds& totalTimeRef, size_t& totalCallsRef)
    {
        benchmarks.emplace_back(title, subtype, typeToStringFunc, totalTimeRef, totalCallsRef);
    }

    static void Reset()
    {
        for (auto& elem : benchmarks)
        {
            std::chrono::microseconds& totalTime = std::get<3>(elem).get();
            size_t& totalCalls = std::get<4>(elem).get();
            totalTime = std::chrono::microseconds::zero();
            totalCalls = 0;
        }
        benchmarks.clear();
    }
};

template <StringLiteral title, int subtype = 0, const char* typeToStringFunc(int) = nullptr>
class BenchmarkT {
    static std::chrono::microseconds totalTime;
    static size_t totalCalls;
    union {
        std::chrono::steady_clock::time_point start;
        long long raw; // time_point raw value.
    };

public:
    BenchmarkT() : start(std::chrono::high_resolution_clock::now())
    {
        if (!totalCalls++) { BenchmarkTCollector::Add(title.value, subtype, typeToStringFunc, totalTime, totalCalls); }
    }
    ~BenchmarkT()
    {
        if(!raw) return;
        std::chrono::steady_clock::time_point end(std::chrono::high_resolution_clock::now());
        std::chrono::microseconds time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        totalTime += time;
    }
    void Stop()
    {
        std::chrono::steady_clock::time_point end(std::chrono::high_resolution_clock::now());
        std::chrono::microseconds time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        totalTime += time;
        raw = 0;
    }

    //const char* GetString() const { return title.value; }
};

template <StringLiteral title, int subtype, const char* typeToStringFunc(int)>
std::chrono::microseconds BenchmarkT<title, subtype, typeToStringFunc>::totalTime;
template <StringLiteral title, int subtype, const char* typeToStringFunc(int)>
size_t BenchmarkT<title, subtype, typeToStringFunc>::totalCalls = 0;

