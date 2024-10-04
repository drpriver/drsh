// Copyright © 2024, David Priver <david@davidpriver.com>
//
// TODOs:
//  - globs
//    - need to roll brace expansion instead of using glob(3)
//    - regex globs maybe?
//  - tab completion
//    - [x] paths
//    - options (use fish completions?)
//    - lister widget for multiple options
//  - command completion
//    - history based?
//  - exec
//    - do we try to fake this on windows or just not support?
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>

// If possible, write functions that work on both
// and then pass this in as a boolean (should get inlined).
// That'll make things more testable cross-platform as tests
// can do it with both variants.
#if defined(_WIN32)
enum {IS_WINDOWS=1};
#else
enum {IS_WINDOWS=0};
#endif
enum OsFlavor {
    OS_APPLE = 0,
    OS_WINDOWS = 1,
    OS_LINUX = 2,
    OS_OTHER = 3,
};
typedef enum OsFlavor OsFlavor;

#if defined(_WIN32)
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#ifdef _MSC_VER
// #pragma warning( disable : 5105)
#endif
#include <Windows.h>
typedef long long ssize_t;

#else // assume posix
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>

// posix_spawn
#include <spawn.h>
#include <sys/wait.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <sys/resource.h>
#endif

#include <glob.h>

#endif
// compiler warnings

#if defined(__GNUC__) || defined(__clang__)
// warnings shared by gcc and clang
#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic error "-Wbad-function-cast"
#pragma GCC diagnostic error "-Wextra"
#pragma GCC diagnostic error "-Wvla"
#pragma GCC diagnostic error "-Wmissing-noreturn"
#pragma GCC diagnostic error "-Wcast-qual"
#pragma GCC diagnostic error "-Wdeprecated"
#pragma GCC diagnostic error "-Wdouble-promotion"
#pragma GCC diagnostic error "-Wint-conversion"
#pragma GCC diagnostic error "-Wimplicit-int"
#pragma GCC diagnostic error "-Wimplicit-function-declaration"
#pragma GCC diagnostic error "-Wincompatible-pointer-types"
#pragma GCC diagnostic error "-Wunused-result"
#pragma GCC diagnostic error "-Wswitch"
#pragma GCC diagnostic error "-Wformat"
#pragma GCC diagnostic error "-Wreturn-type"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

// GCC only warnings
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

// clang only warnings
#if defined(__clang__)
#pragma clang diagnostic error "-Wassign-enum"
#pragma clang diagnostic warning "-Wshadow"
#pragma clang diagnostic error "-Warray-bounds-pointer-arithmetic"
#pragma clang diagnostic error "-Wcovered-switch-default"
#pragma clang diagnostic error "-Wfor-loop-analysis"
#pragma clang diagnostic error "-Winfinite-recursion"
#pragma clang diagnostic warning "-Wduplicate-enum"
#pragma clang diagnostic warning "-Wmissing-field-initializers"
#pragma clang diagnostic error "-Wpointer-type-mismatch"
#pragma clang diagnostic error "-Wextra-tokens"
#pragma clang diagnostic error "-Wmacro-redefined"
#pragma clang diagnostic error "-Winitializer-overrides"
#pragma clang diagnostic error "-Wsometimes-uninitialized"
#pragma clang diagnostic error "-Wunused-comparison"
#pragma clang diagnostic error "-Wundefined-internal"
#pragma clang diagnostic error "-Wnon-literal-null-conversion"
#pragma clang diagnostic error "-Wnullable-to-nonnull-conversion"
#pragma clang diagnostic error "-Wnullability-completeness"
#pragma clang diagnostic error "-Wnullability"
#pragma clang diagnostic warning "-Wuninitialized"
#pragma clang diagnostic warning "-Wconditional-uninitialized"
#pragma clang diagnostic error "-Wundefined-internal"
#pragma clang diagnostic warning "-Wcomma"
#endif

// clang nullability extension
#ifdef __clang__
#pragma clang assume_nonnull begin
#else
#define _Nonnull
#define _Nullable
#define _Null_unspecified
#endif

// function attributes
#ifndef DRSH_INTERNAL
#define DRSH_INTERNAL static
#endif

#ifndef DRSH_FLATTEN
#if defined(__GNUC__)
#define DRSH_FLATTEN __attribute__((__flatten__))
#else
#define DRSH_FLATTEN
#endif
#endif

#ifndef DRSH_INLINE
#define DRSH_INLINE static inline
#endif

#ifndef DRSH_FORCE_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define DRSH_FORCE_INLINE static inline __attribute__((__always_inline__))
#elif defined(_MSC_VER)
#define DRSH_FORCE_INLINE static inline __forceinline
#else
#define DRSH_FORCE_INLINE static inline
#endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#define DRSH_WARN_UNUSED __attribute__((warn_unused_result))
#elif defined(_MSC_VER)
#define DRSH_WARN_UNUSED
#else
#define DRSH_WARN_UNUSED
#endif

//
// Inserts a buffer into an array at the given position.
//
// Arguments:
// ----------
// whence:
//   The offset into the dst buffer to insert at (in bytes).
//
// dst:
//   The destination buffer to insert into.
//
// capacity:
//   The length of the destination buffer (in bytes).
//
// used:
//   How much of the destination buffer has actually been used / should be
//   preserved (in bytes).
//
// src:
//   The buffer to copy from.
//
// length:
//   The length of the src buffer (in bytes).
//
// Returns:
// --------
// 0 on success, 1 on failure.
//
DRSH_INLINE
DRSH_WARN_UNUSED
int
meminsert(
    size_t whence,
    void* restrict dst, size_t capacity, size_t used,
    const void* restrict src, size_t length
){
    if(capacity - used < length) return 1;
    if(whence > used) return 1;
    if(used == whence){
        memmove(((char*)dst)+whence, src, length);
        return 0;
    }
    size_t tail = used - whence;
    memmove(((char*)dst)+whence+length, ((char*)dst)+whence, tail);
    memmove(((char*)dst)+whence, src, length);
    return 0;
}

//
// Appends a buffer to the end of an array.
//
// Arguments:
// ----------
// dst:
//   The destination buffer to insert into.
//
// capacity:
//   The length of the destination buffer (in bytes).
//
// used:
//   How much of the destination buffer has actually been used / should be
//   preserved (in bytes).
//
// src:
//   The buffer to copy from.
//
// length:
//   The length of the src buffer (in bytes).
//
// Returns:
// --------
// 0 on success, 1 on failure.
//
DRSH_INLINE
DRSH_WARN_UNUSED
int
memappend(
    void* restrict dst, size_t capacity, size_t used,
    const void* restrict src, size_t length
){
    if(capacity - used < length) return 1;
    memcpy(((char*)dst)+used, src, length);
    return 0;
}


//
// Logically "removes" a section of an array by shifting the stuff after
// it forward.
//
// This is less error prone than doing the pointer arithmetic at each call
// site. You can pass a buffer + length and where and how much to remove.
//
// Arguments:
// ----------
// whence:
//   The offset to the beginning of the section to remove.
//
// buff:
//   The buffer to remove the section from.
//
// bufflen:
//   The length of the buffer.
//
// nremove:
//   Length of the section to remove.
//
DRSH_INLINE
void
memremove(size_t whence, void* buff, size_t bufflen, size_t nremove){
    assert(nremove + whence <= bufflen);
    size_t tail = bufflen - whence - nremove;
    if(tail) memmove(((char*)buff)+whence, ((char*)buff)+whence+nremove, tail);
}

// hashing

// dummy structs to allow unaligned loads.
// ubsan complains otherwise. This is sort of a grey
// area.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma pack(push)
#pragma pack(1)
typedef struct drsh_packed_uint64 drsh_packed_uint64;
struct drsh_packed_uint64 {
    uint64_t v;
};

typedef struct drsh_packed_uint32 drsh_packed_uint32;
struct drsh_packed_uint32 {
    uint32_t v;
};

typedef struct drsh_packed_uint16 drsh_packed_uint16;
struct drsh_packed_uint16 {
    uint16_t v;
};
#pragma pack(pop)
#else
typedef struct drsh_packed_uint64 drsh_packed_uint64;
struct __attribute__((packed)) drsh_packed_uint64 {
    uint64_t v;
};

typedef struct drsh_packed_uint32 drsh_packed_uint32;
struct __attribute__((packed)) drsh_packed_uint32 {
    uint32_t v;
};

typedef struct drsh_packed_uint16 drsh_packed_uint16;
struct __attribute__((packed)) drsh_packed_uint16 {
    uint16_t v;
};
#endif



#if defined(__ARM_ACLE) && __ARM_FEATURE_CRC32
#if defined(__IMPORTC__)
__import arm_acle;
#else
#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#include <arm_acle.h>

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif
#endif

DRSH_INLINE
uint32_t
drsh_hash_align1(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, (*(const drsh_packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = __crc32cw(h, (*(const drsh_packed_uint32*)k).v);
    for(;len >= 2; k+=2, len-=2)
        h = __crc32ch(h, (*(const drsh_packed_uint16*)k).v);
    for(;len >= 1; k+=1, len-=1)
        h = __crc32cb(h, *(const uint8_t*)k);
    return h;
}

DRSH_INLINE
uint32_t
drsh_hash_align2(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, (*(const drsh_packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = __crc32cw(h, (*(const drsh_packed_uint32*)k).v);
    for(;len >= 2; k+=2, len-=2)
        h = __crc32ch(h, (*(const drsh_packed_uint16*)k).v);
    return h;
}

DRSH_INLINE
uint32_t
drsh_hash_align4(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, (*(const drsh_packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = __crc32cw(h, (*(const drsh_packed_uint32*)k).v);
    return h;
}
DRSH_INLINE
uint32_t
drsh_hash_align8(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, (*(const drsh_packed_uint64*)k).v);
    return h;
}

#elif defined(__x86_64__) && defined(__SSE4_2__)
#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#include <nmmintrin.h>

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

DRSH_INLINE
uint32_t
drsh_hash_align1(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, (*(const drsh_packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = _mm_crc32_u32(h, (*(const drsh_packed_uint32*)k).v);
    for(;len >= 2; k+=2, len-=2)
        h = _mm_crc32_u16(h, (*(const drsh_packed_uint16*)k).v);
    for(;len >= 1; k+=1, len-=1)
        h = _mm_crc32_u8(h, *(const uint8_t*)k);
    return h;
}
DRSH_INLINE
uint32_t
drsh_hash_align2(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, (*(const drsh_packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = _mm_crc32_u32(h, (*(const drsh_packed_uint32*)k).v);
    for(;len >= 2; k+=2, len-=2)
        h = _mm_crc32_u16(h, (*(const drsh_packed_uint16*)k).v);
    return h;
}
DRSH_INLINE
uint32_t
drsh_hash_align4(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, (*(const drsh_packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = _mm_crc32_u32(h, (*(const drsh_packed_uint32*)k).v);
    return h;
}
DRSH_INLINE
uint32_t
drsh_hash_align8(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, (*(const drsh_packed_uint64*)k).v);
    return h;
}
#else // fall back to murmur hash

// cut'n'paste from the wikipedia page on murmur hash
DRSH_FORCE_INLINE
uint32_t
murmur_32_scramble(uint32_t k) {
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    return k;
}
DRSH_FORCE_INLINE
uint32_t
drsh_hash_align1(const void* key_, size_t len){
    const uint8_t* key = key_;
    uint32_t seed = 4253307714;
	uint32_t h = seed;
    uint32_t k;
    /* Read in groups of 4. */
    for (size_t i = len >> 2; i; i--) {
        k = (*(const drsh_packed_uint32*)key).v;
        key += sizeof(uint32_t);
        h ^= murmur_32_scramble(k);
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64;
    }
    /* Read the rest. */
    k = 0;
    for (size_t i = len & 3; i; i--) {
        k <<= 8;
        k |= key[i - 1];
    }
    h ^= murmur_32_scramble(k);
    /* Finalize. */
	h ^= len;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}
DRSH_FORCE_INLINE
uint32_t
drsh_hash_align2(const void* key, size_t len){
    return drsh_hash_align1(key, len);
}
DRSH_FORCE_INLINE
uint32_t
drsh_hash_align4(const void* key, size_t len){
    return drsh_hash_align1(key, len);
}
DRSH_FORCE_INLINE
uint32_t
drsh_hash_align8(const void* key, size_t len){
    return drsh_hash_align1(key, len);
}

#endif

#define drsh_hash_alignany(key, len) \
      (sizeof(*key)&7) == 0? drsh_hash_align8(key, len) \
    : (sizeof(*key)&3) == 0? drsh_hash_align4(key, len) \
    : (sizeof(*key)&1) == 0? drsh_hash_align2(key, len) \
    :                        drsh_hash_align1(key, len)

DRSH_FORCE_INLINE
uint32_t
drsh_fast_reduce32(uint32_t x, uint32_t y){
    return ((uint64_t)x * (uint64_t)y) >> 32;
}

//
// byte_expansion_distance
// -----------------
// Calculates the number of insertions necessary to make needle equal to haystack.
//
// Arguments:
// ----------
//   haystack: pointer to larger string
//   haystack_len: length of haystack
//   needle: pointer to needle string
//   needle_len: length of needle
//
// Returns:
// --------
// The number of insertions necessary to make needle equal to haystack.
// Returns -1 if the input is invalid or if it is impossible.
//
// This function can return -1 in the following circumstances:
//    1. Needle is longer than haystack.
//    2. Needle contains characters not in haystack.
//    3. It is impossible to make them match.
//

static inline
ssize_t
byte_expansion_distance(const char* haystack, size_t haystack_len, const char* needle, size_t needle_len){
    ssize_t difference = 0;
    for(;;){
        if(needle_len > haystack_len) return -1;
        if(!needle_len) {
            difference += haystack_len;
            return difference;
        }
        // Strip off the leading extent that matches
        for(;;){
            if(!needle_len) {
                difference += haystack_len;
                return difference;
            }
            if(!haystack_len) return -1;
            if(*haystack == *needle){
                haystack++; needle++; haystack_len--; needle_len--;
                continue;
            }
            break;
        }
        // Strip off haystack until we find a match, counting each one.
        for(;;){
            if(!haystack_len) return -1;
            if(*haystack == *needle) break;
            difference++; haystack++; haystack_len--;
        }
        // First character now matches. back to top
    }
    return difference;
}

static inline
ssize_t
byte_expansion_distance_icase(const char* haystack, size_t haystack_len, const char* needle, size_t needle_len){
    ssize_t difference = 0;
    for(;;){
        if(needle_len > haystack_len) return -1;
        if(!needle_len) {
            difference += haystack_len;
            return difference;
        }
        // Strip off the leading extent that matches
        for(;;){
            if(!needle_len) {
                difference += haystack_len;
                return difference;
            }
            if(!haystack_len) return -1;
            if((*haystack | 0x20) == (*needle | 0x20)){
                haystack++; needle++; haystack_len--; needle_len--;
                continue;
            }
            break;
        }
        // Strip off haystack until we find a match, counting each one.
        for(;;){
            if(!haystack_len) return -1;
            if((*haystack|0x20) == (*needle|0x20)) break;
            difference++; haystack++; haystack_len--;
        }
        // First character now matches. back to top
    }
    return difference;
}

#ifdef _WIN32
DRSH_INTERNAL
void
myperror(const char* mess){
    DWORD error = GetLastError();
    LPVOID errmess;
FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char*)&errmess,
        0, NULL );
    printf("%lu %s: %s\n", error, mess, (char*)errmess);
}
#endif

#ifndef CASE_0_9
#define CASE_0_9 '0': case '1': case '2': case '3': case '4': case '5': \
    case '6': case '7': case '8': case '9'
#endif
#ifndef CASE_a_z
#define CASE_a_z 'a': case 'b': case 'c': case 'd': case 'e': case 'f': \
    case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': \
    case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': \
    case 'u': case 'v': case 'w': case 'x': case 'y': case 'z'
#endif

#ifndef CASE_A_Z
#define CASE_A_Z 'A': case 'B': case 'C': case 'D': case 'E': case 'F': \
    case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': \
    case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': \
    case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z'
#endif

typedef int DrshEC;
enum {
    EC_OK,
    EC_OOM,
    EC_IO_ERROR,
    EC_ASSERTION_ERROR,
    EC_UNIMPLEMENTED_ERROR,
    EC_VALUE_ERROR,
    EC_EOF,
    EC_NOT_FOUND,
    EC_EXIT,
};

typedef struct DrshStringView DrshStringView;
struct DrshStringView {
    size_t length;
    const char *txt;
};

typedef struct DrshWriteBuffer DrshWriteBuffer;
struct DrshWriteBuffer {
    size_t length;
    void* ptr;
};
typedef struct DrshReadBuffer DrshReadBuffer;
struct DrshReadBuffer {
    size_t length;
    const void* ptr;
};
#define DRSH_SLICE(type) struct {\
    size_t length;\
    type* ptr;\
}
typedef struct DrshArgv DrshArgv;
struct DrshArgv {
    size_t length; // includes final NULL
    const char* const *ptr; // includes final NULL
};
DRSH_FORCE_INLINE
void
drsh_rb_shift(DrshReadBuffer* buff, size_t n){
    if(n > buff->length) n = buff->length;
    buff->ptr = (const char*)buff->ptr + n;
    buff->length -= n;
}
DRSH_FORCE_INLINE
void
drsh_wb_shift(DrshWriteBuffer* buff, size_t n){
    if(n > buff->length) n = buff->length;
    buff->ptr = (char*)buff->ptr + n;
    buff->length -= n;
}

#define EACH_RB(rb, type, iter) type *iter = (rb).ptr, * const iter##__end = (type *)((rb).length+(const char *)(rb).ptr);iter!=iter##__end; iter++

DRSH_INTERNAL
_Bool
drsh_sv_eq(const DrshStringView *a, const DrshStringView *b){
    if(a == b) return 1;
    if(a->length != b->length) return 0;
    if(a->txt == b->txt) return 1;
    return memcmp(a->txt, b->txt, a->length) == 0;
}

DRSH_INTERNAL
int
drsh_sv_cmp(const DrshStringView *a, const DrshStringView *b){
    return memcmp(a->txt, b->txt, a->length);
}

DRSH_INTERNAL
int
DRSH_FLATTEN
drsh_sv_v_cmp(const void *a, const void *b){
    return drsh_sv_cmp(a, b);
}

DRSH_INTERNAL
_Bool
drsh_path_is_abs(DrshStringView path, _Bool windows_style){
    _Bool is_abs = 0;
    if(windows_style){
        if(path.length > 2 && path.txt[1] == ':' && (path.txt[2] == '/' || path.txt[2] == '\\')){
            char c = path.txt[0] | 0x20;
            is_abs = c >= 'a' && c <= 'z';
        }
        is_abs = is_abs || path.txt[0] == '\\';
    }
    is_abs = is_abs || path.txt[0] == '/';
    return is_abs;
}

typedef struct DrshGrowBuffer DrshGrowBuffer;
struct DrshGrowBuffer {
    char* data;
    size_t count;
    size_t cap;
};

DRSH_FORCE_INLINE
void
drsh_gb_clear(DrshGrowBuffer* buff){
    buff->count = 0;
}

DRSH_INLINE
DRSH_WARN_UNUSED
DrshEC
drsh_gb_ensure(DrshGrowBuffer* buff, size_t sz){
    if(buff->count + sz <= buff->cap) return EC_OK;
    size_t new_cap = buff->cap + sz;
    void* p = buff->data?realloc(buff->data, new_cap):malloc(new_cap);
    if(!p) return EC_OOM;
    buff->data = p;
    buff->cap = new_cap;
    return EC_OK;
}

DRSH_INLINE
DRSH_WARN_UNUSED
DrshEC
drsh_gb_ensure2(DrshGrowBuffer* buff, size_t sz, size_t grow_amount){
    if(buff->count + sz <= buff->cap) return EC_OK;
    size_t new_cap = buff->cap + grow_amount;
    void* p = buff->data?realloc(buff->data, new_cap):malloc(new_cap);
    if(!p) return EC_OOM;
    buff->data = p;
    buff->cap = new_cap;
    return EC_OK;
}

DRSH_FORCE_INLINE
DRSH_WARN_UNUSED
DrshEC
drsh_gb_append(DrshGrowBuffer* restrict buff, const void* restrict p, size_t sz){
    int e = memappend(buff->data, buff->cap, buff->count, p, sz);
    if(e) return EC_ASSERTION_ERROR;
    buff->count += sz;
    return EC_OK;
}

DRSH_FORCE_INLINE
DRSH_WARN_UNUSED
DrshEC
drsh_gb_append_(DrshGrowBuffer* restrict buff, const void* restrict p, size_t sz){
    DrshEC err = drsh_gb_ensure(buff, sz);
    if(err) return err;
    int e = memappend(buff->data, buff->cap, buff->count, p, sz);
    if(e) return EC_ASSERTION_ERROR;
    buff->count += sz;
    return EC_OK;
}

DRSH_FORCE_INLINE
DRSH_WARN_UNUSED
DrshEC
drsh_gb_insert(size_t whence, DrshGrowBuffer* restrict buff, const void* restrict p, size_t sz){
    int e = meminsert(whence, buff->data, buff->cap, buff->count, p, sz);
    if(e) return EC_ASSERTION_ERROR;
    buff->count += sz;
    return EC_OK;
}

DRSH_FORCE_INLINE
DrshWriteBuffer
drsh_gb_writable_buffer(const DrshGrowBuffer* buff){
    return (DrshWriteBuffer){
        .ptr = buff->data+buff->count,
        .length = buff->cap - buff->count,
    };
}

DRSH_FORCE_INLINE
DrshReadBuffer
drsh_gb_readable_buffer(const DrshGrowBuffer* buff){
    return (DrshReadBuffer){
        .ptr = buff->data,
        .length = buff->count,
    };
}

DRSH_FORCE_INLINE
_Bool
drsh_rb_iendswith2(DrshReadBuffer rb, const void* mem, size_t len){
    if(rb.length < len) return 0;
    const char* a = rb.length-len+(const char*)rb.ptr;
    const char* b = mem;
    for(size_t i = 0; i < len; i++)
        if((a[i]|0x20) != (b[i]|0x20)) return 0;
    return 1;
}

typedef struct DrshAtom DrshAtom;

typedef struct DrshInput DrshInput;
struct DrshInput {
    DrshGrowBuffer read_buffer;
    size_t read_cursor;
    DrshGrowBuffer write_buffer;
    size_t write_cursor;
    DrshReadBuffer prompt;
    size_t prompt_display_len;
    _Bool needs_redisplay;
    _Bool needs_clear_screen;

    size_t hist_start; // marks previously loaded history
    DrshGrowBuffer hist_buffer;
    size_t hist_cursor;
    DrshGrowBuffer prompt_buffer;
    size_t prompt_visual_len;

    _Bool tab_completion;
    DrshGrowBuffer tab_completions;
    size_t tab_completion_cursor;
};

enum {
    CMD_MOVE_HOME             = -1,   // ctrl-a, home
    CMD_MOVE_LEFT             = -2,   // ctrl-b, left arrow
    CMD_INTERRUPT             = -3,   // ctrl-c
    CMD_DELETE_FORWARD_OR_EOF = -4,   // ctrl-d
    CMD_MOVE_END              = -5,   // ctrl-e
    CMD_MOVE_RIGHT            = -6,   // ctrl-f, right arrow
    CMD_CTRL_G                = -7,   // ctrl-g
    CMD_DELETE_BACK           = -8,   // ctrl-h, backspace
    CMD_TAB                   = -9,   // tab, ctrl-i
    CMD_ACCEPT                = -10,  // ctrl-j, newline
    CMD_KILL_END_OF_LINE      = -11,  // ctrl-k
    CMD_CLEAR_SCREEN          = -12,  // ctrl-l
    CMD_ENTER                 = -13,  // ctrl-m, enter
    CMD_MOVE_DOWN             = -14,  // ctrl-n, down arrow
    CMD_CTRL_O                = -15,  // ctrl-o
    CMD_MOVE_UP               = -16,  // ctrl-p, up arrow
    CMD_CTRL_Q                = -17,  // ctrl-q
    CMD_CTRL_R                = -18,  // ctrl-r
    CMD_CTRL_S                = -19,  // ctrl-s
    CMD_CTRL_T                = -20,  // ctrl-t
    CMD_CTRL_U                = -21,  // ctrl-u
    CMD_CTRL_V                = -22,  // ctrl-v
    CMD_CTRL_W                = -23,  // ctrl-w
    CMD_CTRL_X                = -24,  // ctrl-x
    CMD_CTRL_Y                = -25,  // ctrl-y
    CMD_CTRL_Z                = -26,  // ctrl-z
    CMD_ESC                   = -27,  // escape
    CMD_NOP                   = -28,
    // CMD_BACKSPACE             = -127, // backspace
    CMD_DELETE_FORWARD        = -128, // delete
    CMD_SHIFT_TAB             = -129, // shift+tab
};

DRSH_INTERNAL void drsh_inp_move_home(DrshInput* inp);
DRSH_INTERNAL void drsh_inp_move_end(DrshInput* inp);
DRSH_INTERNAL void drsh_inp_move_left(DrshInput* inp);
DRSH_INTERNAL void drsh_inp_move_right(DrshInput* inp);
DRSH_INTERNAL void drsh_inp_move_up(DrshInput* inp);
DRSH_INTERNAL void drsh_inp_move_down(DrshInput* inp);
DRSH_INTERNAL void drsh_inp_del_left(DrshInput* inp);
DRSH_INTERNAL void drsh_inp_del_right(DrshInput* inp);
DRSH_INTERNAL void drsh_inp_kill_end_of_line(DrshInput* inp);
DRSH_INTERNAL void drsh_inp_clear(DrshInput* inp);
DRSH_INTERNAL void drsh_end_tab_completion(DrshInput* inp);
typedef struct DrshEnvironment DrshEnvironment;
DRSH_INTERNAL void drsh_tab_completion(DrshInput* inp, DrshEnvironment*);
DRSH_INTERNAL void drsh_tab_completion_prev(DrshInput* inp);
DRSH_INTERNAL void drsh_tab_completion_cancel(DrshInput* inp);
DRSH_WARN_UNUSED
DRSH_INTERNAL DrshEC drsh_inp_input_one(DrshInput* inp, unsigned char c);


#define ATOM_X(apply) \
    apply(pwd) \
    apply(cd) \
    apply(echo) \
    apply(set) \
    apply(exit) \
    apply(source) \
    apply(time) \
    apply(PWD) \
    apply(HOME) \
    apply(PATH) \
    apply(PATHEXT) \
    apply(COLUMNS) \
    apply(LINES) \
    apply(TERM) \
    apply(USER) \
    apply(SHELL) \
    apply(SHLVL) \
    apply(DRSH_HISTORY) \
    apply(DRSH_CONFIG) \
    apply(debug) \
    apply(on) \
    apply(off) \
    apply(true) \
    apply(false) \
    apply(0) \
    apply(1) \

enum {
#define X(x) ATOM_##x,
    ATOM_X(X)
#undef X
    ATOM_DOT,
    ATOM_MAX,
};

typedef struct DrshAtom DrshAtom;
typedef struct DrshAtomTable DrshAtomTable;
struct DrshAtomTable {
    void* data;
    size_t cap;
    size_t count;
    const DrshAtom*_Nonnull special[ATOM_MAX];
};

struct DrshAtom {
    uint32_t len;
    uint32_t hash;
    const DrshAtom* iatom;
    char txt[];
};

DRSH_FORCE_INLINE
DrshStringView
drsh_atom_sv(const DrshAtom* a){
    return (DrshStringView){a->len, a->txt};
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_at_init(DrshAtomTable* at);


DRSH_INTERNAL
int
drsh_atom_cmp(const void* a_, const void* b_){
    const DrshAtom* a = *(const DrshAtom*const*)a_;
    const DrshAtom* b = *(const DrshAtom*const*)b_;
    if(a == b) return 0;
    int c = strcmp(a->txt, b->txt);
    // printf("%s cmp %s? %d\r\n", a->txt, b->txt, c);
    return c;
}

DRSH_INTERNAL
int
drsh_atom_icmp(const void* a_, const void* b_){
    const DrshAtom* a = *(const DrshAtom*const*)a_;
    const DrshAtom* b = *(const DrshAtom*const*)b_;
    return drsh_atom_cmp(&a->iatom, &b->iatom);
}

DRSH_INTERNAL
_Bool
drsh_atom_ieq(const DrshAtom* a, const DrshAtom* b){
    return a->iatom == b->iatom;
}

typedef struct DrshEnvironment DrshEnvironment;
struct DrshEnvironment {
    DrshAtomTable* at;
    DrshGrowBuffer cwd;
    DrshGrowBuffer tmp;
    const DrshAtom*_Nullable home;
    void* data;
    size_t cap;
    size_t count;
    _Bool sorted;
    _Bool case_insensitive;
    _Bool debug;
    int cols, lines;
    OsFlavor os_flavor;
};

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_init(DrshEnvironment* env, DrshAtomTable* at, void* envp_, _Bool windows_style);

DRSH_INTERNAL
void
drsh_env_sort_env(DrshEnvironment* env);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_set_env(DrshEnvironment* env, const DrshAtom* key, const DrshAtom* value);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_set_env3(DrshEnvironment* env, const DrshAtom* key, const char* value_txt, size_t value_len);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_set_env4(DrshEnvironment* env, const char* key_txt, size_t key_len, const char* value_txt, size_t value_len);

DRSH_INTERNAL
const DrshAtom*_Nullable
drsh_env_get_env(DrshEnvironment* env, const DrshAtom* key);

DRSH_INTERNAL
const DrshAtom*_Nullable
drsh_env_get_env2(DrshEnvironment* env, const char* key, size_t len);

DRSH_INTERNAL
void*_Nullable
drsh_env_get_envp(DrshEnvironment* env, _Bool windows_style);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_get_config_path(DrshEnvironment* env, DrshGrowBuffer*);

DRSH_INTERNAL
DrshEC
drsh_env_set_shell_path(DrshEnvironment* env);

DRSH_INTERNAL
DrshEC
drsh_env_increment_shlvl(DrshEnvironment* env);


DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_get_history_path(DrshEnvironment* env, const DrshAtom**);

DRSH_FORCE_INLINE
const DrshAtom*
drsh_unsafe_string_to_atom(const char* p){
    return (const DrshAtom*)(p - sizeof(DrshAtom));
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_at_atomize(DrshAtomTable*restrict at, const char* restrict txt, size_t length, const DrshAtom**restrict out_atom);


typedef struct DrshToken DrshToken;
struct DrshToken {
    size_t length;
    const char* txt;
};

typedef struct DrshTokenized DrshTokenized;
struct DrshTokenized {
    DrshGrowBuffer token_buffer;
};

typedef struct DrshTermState DrshTermState;

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_tokenize_line(const DrshReadBuffer *rb, DrshTokenized *t);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_process_line(const DrshReadBuffer *rb, DrshEnvironment* env, DrshAtomTable* at, DrshTokenized *t, DrshGrowBuffer* tok_argv, DrshTermState* ts, DrshGrowBuffer* tmp);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_source_file(const DrshAtom* path, DrshEnvironment* env, DrshAtomTable* at, DrshTokenized *tokens, DrshGrowBuffer* tok_argv, DrshTermState* ts, DrshGrowBuffer* tmp);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_tokens_to_argv(DrshReadBuffer toks, DrshEnvironment* env, DrshAtomTable* at, DrshGrowBuffer* outbuff);


DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_read_line(DrshTermState* ts, DrshGrowBuffer*, DrshInput*, DrshEnvironment*, DrshReadBuffer*);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_read_one(DrshTermState* ts, DrshInput*, int*);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_refresh_cwd(DrshEnvironment* env, _Bool backslash_is_sep);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_refresh_size(DrshTermState* ts, DrshEnvironment* env);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_chdir(DrshEnvironment* env, const DrshArgv*);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_refresh_prompt(DrshInput* input, DrshEnvironment* env);

#ifdef _WIN32
typedef HANDLE FileHandle;
#else
typedef int FileHandle;
#endif

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_read_file(const char*restrict filepath, DrshGrowBuffer* outbuff);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_open_file_for_appending_with_mkdirs(const char* path, size_t length, FileHandle* fh);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_close_file(FileHandle fh);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_append_to_file(FileHandle fh, const char* txt, size_t length);

DRSH_INTERNAL
size_t
drsh_rb_to_line(const DrshReadBuffer* restrict inrb, DrshReadBuffer* restrict outrb);

enum {
    TS_INIT = 0,
    TS_RAW = 1,
    TS_ORIG = 2,
    TS_UNKNOWN = 3,
};


DRSH_INLINE
DRSH_WARN_UNUSED
DrshEC
drsh_get_io_handles(FileHandle* in_handle, FileHandle* out_handle){
    #ifdef _WIN32
    *in_handle  = GetStdHandle(STD_INPUT_HANDLE);
    if(*in_handle == INVALID_HANDLE_VALUE){
        return EC_IO_ERROR;
    }
    *out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if(*out_handle == INVALID_HANDLE_VALUE){
        return 1;
    }
    #else
    *in_handle = STDIN_FILENO;
    *out_handle = STDOUT_FILENO;
    #endif
    return EC_OK;
}

DRSH_INTERNAL
_Bool
drsh_exists(const char* path);


struct DrshTermState {
    int state;
    _Bool in_is_terminal, out_is_terminal;
    FileHandle in_fd, out_fd;
    #if defined(_WIN32)
        DWORD in_orig, in_raw;
        DWORD out_orig, out_raw;
    #else
        struct termios orig, raw;
    #endif
    DrshGrowBuffer tmp;
};

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_init(DrshTermState* ts, FileHandle in_fd, FileHandle out_fd);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_raw(DrshTermState* ts);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_unknown(DrshTermState* ts);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_orig(DrshTermState* ts);

DRSH_INTERNAL
DrshEC
drsh_ts_write(DrshTermState*restrict ts, const void*restrict p, size_t len);

DRSH_INTERNAL
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
DrshEC
drsh_ts_printf(DrshTermState*restrict ts, const char* fmt, ...);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_vprintf(DrshTermState*restrict ts, const char* fmt, va_list);

DRSH_INTERNAL
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
DRSH_WARN_UNUSED
DrshEC
drsh_gb_sprintf(DrshGrowBuffer* gb, const char* fmt, ...);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_gb_vsprintf(DrshGrowBuffer* gb, const char* fmt, va_list);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_buffered_write(DrshTermState*restrict ts, const void*restrict p, size_t len);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_hist_add(DrshInput* inp, const DrshAtom* atom);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_hist_dump(const DrshInput* input, DrshEnvironment* env);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_spawn_process_and_wait(DrshTermState* ts, DrshEnvironment* env, DrshGrowBuffer* tmp, const char*const* argv, _Bool report_time);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_resolve_prog_path(DrshEnvironment* env, DrshGrowBuffer* tmp, const DrshAtom* program, _Bool windows_style);

#ifdef _WIN32
#define MAIN(argc, argv) main(argc, argv)
#else
#define MAIN(argc, argv) main(argc, argv, char** envp)
#endif

int
MAIN(int argc, char** argv){
    DrshTermState ts = {0};
    FileHandle in_handle, out_handle;
    if(drsh_get_io_handles(&in_handle, &out_handle) != EC_OK)
        return 1;
    DrshInput input = {0};
    input.prompt = (DrshReadBuffer){2, "> "};
    DrshEC err = drsh_ts_init(&ts, in_handle, out_handle);
    if(err) return 0;
    DrshAtomTable at = {0};
    err = drsh_at_init(&at);
    if(err) return 1;
    DrshEnvironment env = {0};
    #ifdef _WIN32
    for(void* envp = GetEnvironmentStrings(); envp; FreeEnvironmentStrings(envp), envp=NULL)
    #endif
    err = drsh_env_init(&env, &at, envp, IS_WINDOWS);
    if(err) return 1;
    DrshTokenized tokens = {0};
    DrshGrowBuffer tok_argv = {0};
    DrshGrowBuffer termbuff = {0};
    DrshGrowBuffer tmp = {0};
    err = drsh_refresh_cwd(&env, IS_WINDOWS);
    if(err) return 1;
    err = drsh_refresh_size(&ts, &env);
    if(err) return 1;
    {
        err = drsh_env_set_shell_path(&env);
        if(err) printf("error setting SHELL\r\n");
        (void)err;
    }
    {
        err = drsh_env_increment_shlvl(&env);
        (void)err;
    }
    #define SHOW_CURSOR "\033[?25h"
    drsh_ts_write(&ts, SHOW_CURSOR, -1+sizeof SHOW_CURSOR);
    err = drsh_env_get_config_path(&env, &env.tmp);
    if(!err){
        const DrshAtom* DRSH_CONFIG = at.special[ATOM_DRSH_CONFIG];
        const DrshAtom* config_path;
        err = drsh_at_atomize(&at, env.tmp.data, env.tmp.count, &config_path);
        if(err) return 1;
        err = drsh_env_set_env(&env, DRSH_CONFIG, config_path);
        if(err) return 1;
        err = drsh_source_file(config_path, &env, &at, &tokens, &tok_argv, &ts, &tmp);
        if(err == EC_EXIT) return 0;
        err = EC_OK;
    }
    for(int i = 1; i < argc; i++){
        const DrshAtom* path;
        err = drsh_at_atomize(&at, argv[i], strlen(argv[i]), &path);
        if(err) return 1;
        err = drsh_source_file(path, &env, &at, &tokens, &tok_argv, &ts, &tmp);
        if(err == EC_EXIT) return 0;
        err = EC_OK;
    }
    if(argc > 1) goto Lfinish;
    {
        const DrshAtom* drsh_history_path;
        err = drsh_env_get_history_path(&env, &drsh_history_path);
        if(!err){
            drsh_gb_clear(&tmp);
            err = drsh_read_file(drsh_history_path->txt, &tmp);
            if(!err){
                DrshReadBuffer history = drsh_gb_readable_buffer(&tmp);
                DrshReadBuffer line;
                for(;;){
                    size_t len = drsh_rb_to_line(&history, &line);
                    if(!len) break;
                    drsh_rb_shift(&history, len);
                    while(line.length && (((const char*)line.ptr)[line.length-1] == '\n' || ((const char*)line.ptr)[line.length-1] == '\r'))
                        line.length--;
                    if(!line.length) continue;
                    const DrshAtom* l; err = drsh_at_atomize(&at, line.ptr, line.length, &l);
                    if(!err){
                        err = drsh_hist_add(&input, l);
                        (void) err;
                    }
                }
                input.hist_start = input.hist_cursor;
            }
            else {
                drsh_ts_printf(&ts, "error reading: %s\r\n", drsh_history_path->txt);
            }
        }
        else {
            drsh_ts_printf(&ts, "error getting history path\r\n");
        }
    }
    for(;;){
        DrshReadBuffer input_line;
        err = drsh_read_line(&ts, &termbuff, &input, &env, &input_line);
        if(ts.in_is_terminal && ts.out_is_terminal) drsh_ts_write(&ts, "\r\n", 2);
        if(err) break;
        err = drsh_refresh_size(&ts, &env);
        (void)err;
        if(ts.in_is_terminal){
            const DrshAtom* input_atom;
            err = drsh_at_atomize(&at, input_line.ptr, input_line.length, &input_atom);
            if(!err){
                err = drsh_hist_add(&input, input_atom);
                (void)err;
            }
        }
        err = drsh_process_line(&input_line, &env, &at, &tokens, &tok_argv, &ts, &tmp);
        if(err == EC_EXIT)
            break;
    }
    err = drsh_hist_dump(&input, &env);
    (void)err;
    Lfinish:;
    err = drsh_ts_orig(&ts);
    (void)err;
    return 0;
}

DRSH_INTERNAL
size_t
drsh_rb_to_line(const DrshReadBuffer* restrict inrb, DrshReadBuffer* restrict outrb){
    const char* txt = inrb->ptr;
    size_t length = inrb->length;
    for(size_t i = 0; i < length; i++){
        char c = txt[i];
        if(c == '\0' || c == '\n' || c == '\r'){
            outrb->ptr = txt;
            outrb->length = i+1;
            return i+1;
        }
    }
    return 0;
}

DRSH_INTERNAL
size_t
drsh_rb_to_cmd(const DrshReadBuffer* rb, int* cmd){
    size_t length = rb->length;
    const unsigned char* txt = rb->ptr;
    if(!length) return 0;
    unsigned char c = txt[0];
    if(c < 27){
        *cmd = -(int)c;
        return 1;
    }
    if(c == 127){
        *cmd = CMD_DELETE_BACK;
        return 1;
    }
    if(c > 27){
        *cmd = c;
        return 1;
    }
    if(c == 27){
        if(length > 2){
            if(txt[1] == '['){
                if(txt[2] >= '0' && txt[2] <= '9' && length > 3){
                    if(txt[3] == '~'){
                        switch(txt[2]){
                            case '3':
                                *cmd = CMD_DELETE_FORWARD;
                                return 4;
                        }
                    }
                }
                switch(txt[2]){
                    case 'A':
                        *cmd = CMD_MOVE_UP;
                        return 3;
                    case 'B':
                        *cmd = CMD_MOVE_DOWN;
                        return 3;
                    case 'C':
                        *cmd = CMD_MOVE_RIGHT;
                        return 3;
                    case 'D':
                        *cmd = CMD_MOVE_LEFT;
                        return 3;
                    case 'H':
                        *cmd = CMD_MOVE_HOME;
                        return 3;
                    case 'F':
                        *cmd = CMD_MOVE_END;
                        return 3;
                    case 'Z':
                        *cmd = CMD_SHIFT_TAB;
                        return 3;
                }
                return 0;
            }
            if(txt[1] == 'O'){
                switch(txt[2]){
                    case 'H':
                        *cmd = CMD_MOVE_HOME;
                        return 3;
                    case 'F':
                        *cmd = CMD_MOVE_END;
                        return 3;
                }
            }
        }
        *cmd = CMD_ESC;
        return 1;
    }
    return 0;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_read_one(DrshTermState* ts, DrshInput* inp, int* cmd){
    for(;;){
        if(inp->read_cursor){
            if(inp->read_cursor == inp->read_buffer.count){
                drsh_gb_clear(&inp->read_buffer);
                inp->read_cursor = 0;
            }
            else {
                DrshReadBuffer rb = drsh_gb_readable_buffer(&inp->read_buffer);
                drsh_rb_shift(&rb, inp->read_cursor);
                size_t len = drsh_rb_to_cmd(&rb, cmd);
                if(len){
                    inp->read_cursor += len;
                    return EC_OK;
                }
            }
        }
        DrshEC err = EC_OK;
        err = drsh_gb_ensure(&inp->read_buffer, inp->read_cursor+8000);
        if(err) return err;
        DrshWriteBuffer tb = drsh_gb_writable_buffer(&inp->read_buffer);
        #ifdef _WIN32
            DWORD nread;
            BOOL ok = ReadFile(ts->in_fd, tb.ptr, (DWORD)tb.length, &nread, NULL);
            (void)ok;
        #else
            ssize_t nread = read(ts->in_fd, tb.ptr, tb.length);
            if(nread < 0 && errno == EINTR) continue;
        #endif
        if(nread < 0) {
            return EC_IO_ERROR;
        }
        if(nread == 0){
            return EC_IO_ERROR;
        }
        inp->read_buffer.count += nread;
        DrshReadBuffer rb = drsh_gb_readable_buffer(&inp->read_buffer);
        drsh_rb_shift(&rb, inp->read_cursor);
        size_t len = drsh_rb_to_cmd(&rb, cmd);
        inp->read_cursor += len;
        if(len) return EC_OK;
    }
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_read_line(DrshTermState* ts, DrshGrowBuffer* termbuff, DrshInput* inp, DrshEnvironment* env, DrshReadBuffer* outbuf){
    DrshEC err = EC_OK;
    err = drsh_ts_raw(ts);
    drsh_gb_clear(&inp->write_buffer);
    inp->needs_redisplay = 1;
    inp->write_cursor = 0;
    if(err) return err;
    size_t n_cols_up = 0;
    // FILE* log = fopen("log.txt", "wb");
    for(;;){
        // fprintf(log, "----\n");
        if(inp->needs_redisplay && ts->in_is_terminal && ts->out_is_terminal){
            err = drsh_refresh_size(ts, env);
            (void)err;
            err = drsh_refresh_prompt(inp, env);
            if(err) break;
            drsh_gb_clear(termbuff);
            if(inp->needs_clear_screen){
                err = drsh_gb_sprintf(termbuff, "\033[2J\033[1;1H");
                inp->needs_clear_screen = 0;
            }
            // fprintf(log, "Going n cols up: %zu\n", n_cols_up);
            if(n_cols_up){
                err = drsh_gb_sprintf(termbuff, "\033[%dA", (int)n_cols_up);
                if(err) return err;
            }
            err = drsh_gb_append_(termbuff, "\r\033[J", 4);
            if(err) return err;
            err = drsh_gb_append_(termbuff, inp->prompt.ptr, inp->prompt.length);
            if(err) return err;
            size_t visual_size = inp->prompt_visual_len;
            if(inp->write_buffer.count){
                err = drsh_gb_append_(termbuff, inp->write_buffer.data, inp->write_buffer.count);
                if(err) return err;
                visual_size += inp->write_buffer.count;
            }
            if(1){
                size_t cursor_visual_position = visual_size - (inp->write_buffer.count-inp->write_cursor);
                size_t total_lines = (visual_size-1) / env->cols + 1;
                size_t cursor_line = (cursor_visual_position-1) / env->cols+1;
                n_cols_up = total_lines - 1;
                if(total_lines > cursor_line){
                    int diff = total_lines - cursor_line;
                    err = drsh_gb_sprintf(termbuff, "\033[%dA", diff);
                    if(err) return err;
                    n_cols_up -= diff;
                }
                int cur_col = (cursor_visual_position-1) % env->cols + 1;
                err = drsh_gb_sprintf(termbuff, "\r\033[%dC", cur_col);
                if(err) return err;
                // fprintf(log, "env->cols: %d\n", env->cols);
                // fprintf(log, "visual_size: %zu\n", visual_size);
                // fprintf(log, "cursor_visual_position: %zu\n", cursor_visual_position);
                // fprintf(log, "total_lines: %zu\n", total_lines);
                // fprintf(log, "cursor_line: %zu\n", cursor_line);
                // fprintf(log, "n_cols_up: %zu\n", n_cols_up);
                // fprintf(log, "cur_col: %d\n", cur_col);
                // fflush(log);
            }
            DrshReadBuffer rb_ = drsh_gb_readable_buffer(termbuff);
            drsh_ts_write(ts, rb_.ptr, rb_.length);
            inp->needs_redisplay = 0;
        }
        int cmd;
        err = drsh_read_one(ts, inp, &cmd);
        if(err) return err;
        if(cmd != CMD_TAB && cmd != CMD_SHIFT_TAB && cmd != CMD_ESC)
            drsh_end_tab_completion(inp);
        if(cmd < 0){
            switch(cmd){
                case CMD_DELETE_BACK:
                    drsh_inp_del_left(inp);
                    break;
                case CMD_DELETE_FORWARD_OR_EOF:
                    if(!inp->write_buffer.count)
                        return EC_EOF;
                    #ifdef __GNUC__
                    __attribute__((__fallthrough__));
                    #endif
                    // fallthrough
                case CMD_DELETE_FORWARD:
                    drsh_inp_del_right(inp);
                    break;
                case CMD_MOVE_RIGHT:
                    drsh_inp_move_right(inp);
                    break;
                case CMD_MOVE_LEFT:
                    drsh_inp_move_left(inp);
                    break;
                case CMD_MOVE_UP:
                    drsh_inp_move_up(inp);
                    break;
                case CMD_MOVE_DOWN:
                    drsh_inp_move_down(inp);
                    break;
                case CMD_MOVE_HOME:
                    drsh_inp_move_home(inp);
                    break;
                case CMD_MOVE_END:
                    drsh_inp_move_end(inp);
                    break;
                case CMD_INTERRUPT:
                    drsh_inp_clear(inp);
                    break;
                case CMD_TAB:
                    drsh_tab_completion(inp, env);
                    break;
                case CMD_SHIFT_TAB:
                    drsh_tab_completion_prev(inp);
                    break;
                case CMD_KILL_END_OF_LINE:
                    drsh_inp_kill_end_of_line(inp);
                    break;
                case CMD_CLEAR_SCREEN:
                    inp->needs_clear_screen = 1;
                    inp->needs_redisplay = 1;
                    break;
                case CMD_ACCEPT:
                case CMD_ENTER:
                    // err = drsh_gb_append_(&inp->write_buffer, "\0", 1);
                    // if(err) return err;
                    *outbuf = drsh_gb_readable_buffer(&inp->write_buffer);
                    return EC_OK;
                case CMD_CTRL_G: break;
                case CMD_CTRL_O: break;
                case CMD_CTRL_Q: break;
                case CMD_CTRL_R: break;
                case CMD_CTRL_S: break;
                case CMD_CTRL_T: break;
                case CMD_CTRL_U: break;
                case CMD_CTRL_V: break;
                case CMD_CTRL_W: break;
                case CMD_CTRL_X: break;
                case CMD_CTRL_Y: break;
                case CMD_CTRL_Z: break;
                case CMD_ESC:
                    if(inp->tab_completion){
                        drsh_tab_completion_cancel(inp);
                        break;
                    }
                    break;
                // case CMD_BACKSPACE:
                default:
                    break;
            }

        }
        else {
            unsigned char c = (unsigned char)(unsigned)cmd;
            // if(c < 127)
            err = drsh_inp_input_one(inp, c);
            if(err) return err;
        }

    }
    if(inp->read_cursor){
        if(inp->read_cursor == inp->read_buffer.count){
            drsh_gb_clear(&inp->read_buffer);
            inp->read_cursor = 0;
        }
        else {
            DrshReadBuffer read_buff = drsh_gb_readable_buffer(&inp->read_buffer);
            drsh_rb_shift(&read_buff, inp->read_cursor);
            size_t len = drsh_rb_to_line(&read_buff, outbuf);
            if(len){
                inp->read_cursor += len;
                return EC_OK;
            }
        }
    }
    err = drsh_gb_ensure(&inp->read_buffer, inp->read_cursor+8000);
    if(err) return err;
    DrshWriteBuffer tb = drsh_gb_writable_buffer(&inp->read_buffer);
    #ifdef _WIN32
        DWORD nread;
        BOOL ok = ReadFile(ts->in_fd, tb.ptr, (DWORD)tb.length, &nread, NULL);
        if(!ok) return EC_IO_ERROR;
    #else
        ssize_t nread = read(ts->in_fd, tb.ptr, tb.length);
        if(nread < 0) {
            return EC_IO_ERROR;
        }
    #endif
    inp->read_buffer.count += nread;
    DrshReadBuffer read_buff = drsh_gb_readable_buffer(&inp->read_buffer);
    drsh_rb_shift(&read_buff, inp->read_cursor);
    size_t len = drsh_rb_to_line(&read_buff, outbuf);
    if(len) inp->read_cursor += len;
    else {
        *outbuf = read_buff;
    }
    return EC_OK;
}

#ifdef _WIN32
static
BOOL
WINAPI
HandlerRoutine(DWORD dwCtrlType){
    (void)dwCtrlType;
    return TRUE;
}
#endif

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_init(DrshTermState* ts, FileHandle in_fd, FileHandle out_fd){
    ts->in_fd = in_fd;
    ts->out_fd = out_fd;
    #ifdef _WIN32
        SetConsoleCtrlHandler(&HandlerRoutine, TRUE);
        ts->in_is_terminal = GetFileType(in_fd) == FILE_TYPE_CHAR;
        ts->out_is_terminal = GetFileType(out_fd) == FILE_TYPE_CHAR;
        if(ts->in_is_terminal){
            assert(in_fd != INVALID_HANDLE_VALUE);
            assert(ts->in_fd != INVALID_HANDLE_VALUE);
            BOOL success;
            ts->in_orig = 0;
            success = GetConsoleMode(ts->in_fd, &ts->in_orig);
            if(!success) return EC_IO_ERROR;
            success = SetConsoleCP(65001);
            if(!success) return EC_IO_ERROR;
        }
        if(ts->out_is_terminal){
            BOOL success;
            ts->out_orig = 0;
            success = GetConsoleMode(ts->out_fd, &ts->out_orig);
            if(!success) return EC_IO_ERROR;
            success = SetConsoleOutputCP(65001);
            if(!success) return EC_IO_ERROR;
        }
    #else
        ts->in_is_terminal = isatty(in_fd);
        ts->out_is_terminal = isatty(out_fd);
        if(ts->in_is_terminal)
            if(tcgetattr(ts->in_fd, &ts->orig) == -1)
                return EC_IO_ERROR;
    #endif
    ts->state = TS_INIT;
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_raw(DrshTermState* ts){
    if(ts->state == TS_RAW) return EC_OK;
    #ifdef _WIN32
        if(ts->in_is_terminal){
            BOOL success;
            ts->in_raw = 0
                | ENABLE_VIRTUAL_TERMINAL_INPUT
                | 0;
            success = SetConsoleMode(ts->in_fd, ts->in_raw);
            if(!success) return EC_IO_ERROR;
        }
        if(ts->out_is_terminal){
            BOOL success;
            ts->out_raw = 0
                | ENABLE_PROCESSED_OUTPUT
                | ENABLE_WRAP_AT_EOL_OUTPUT
                | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                | DISABLE_NEWLINE_AUTO_RETURN
                | 0;
            success = SetConsoleMode(ts->out_fd, ts->out_raw);
            if(!success) return EC_IO_ERROR;
        }
    #else
        if(ts->in_is_terminal){
            ts->raw = ts->orig;
            ts->raw.c_iflag &= ~(0lu
                    | BRKINT // no break
                    | ICRNL  // don't map CR to NL
                    | INPCK  // skip parity check
                    | ISTRIP // don't strip 8th bit off char
                    | IXON   // don't allow start/stop input control
                    );
            ts->raw.c_oflag &= ~(0lu
                | OPOST // disable post processing
                );
            ts->raw.c_cflag |= CS8; // 8 bit chars
            ts->raw.c_lflag &= ~(0lu
                    | ECHO    // disable echo
                    | ICANON  // disable canonical processing
                    | IEXTEN  // no extended functions
                    // Currently allowing these so ^Z works, could disable them
                    | ISIG    // disable signals
                    );
            ts->raw.c_cc[VMIN] = 1; // read every single byte
            ts->raw.c_cc[VTIME] = 0; // no timeout

            // set and flush
            // Change will ocurr after all output has been transmitted.
            // Unread input will be discarded.
            if(tcsetattr(ts->in_fd, TCSAFLUSH, &ts->raw) < 0)
                return EC_IO_ERROR;
        }
    #endif
    ts->state = TS_RAW;
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_unknown(DrshTermState* ts){
    ts->state = TS_UNKNOWN;
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_orig(DrshTermState* ts){
    if(ts->state == TS_ORIG) return EC_OK;
    #ifdef _WIN32
        if(ts->in_is_terminal){
            SetConsoleMode(ts->in_fd, ts->in_orig);
        }
        if(ts->out_is_terminal){
            SetConsoleMode(ts->out_fd, ts->out_orig);
        }
    #else
        if(ts->in_is_terminal){
            if(tcsetattr(ts->in_fd, TCSAFLUSH, &ts->orig) < 0)
                return EC_IO_ERROR;
        }
    #endif
    ts->state = TS_ORIG;
    return EC_OK;
}

DRSH_INTERNAL void drsh_inp_move_home(DrshInput* inp){
    inp->write_cursor = 0;
    inp->needs_redisplay = 1;
}
DRSH_INTERNAL void drsh_inp_move_end(DrshInput* inp){
    inp->write_cursor = inp->write_buffer.count;
    inp->needs_redisplay = 1;
}
DRSH_INTERNAL void drsh_inp_move_left(DrshInput* inp){
    // XXX: unicode
    if(inp->write_cursor) inp->write_cursor--;
    inp->needs_redisplay = 1;
}
DRSH_INTERNAL void drsh_inp_move_right(DrshInput* inp){
    // XXX: unicode
    if(inp->write_cursor < inp->write_buffer.count) inp->write_cursor++;
    inp->needs_redisplay = 1;
}
DRSH_INTERNAL void drsh_inp_move_up(DrshInput* inp){
    if(inp->hist_cursor) inp->hist_cursor--;
    else return;
    inp->needs_redisplay = 1;
    const DrshAtom* atom = ((const DrshAtom**)inp->hist_buffer.data)[inp->hist_cursor];
    drsh_gb_clear(&inp->write_buffer);
    DrshEC err = drsh_gb_append_(&inp->write_buffer, atom->txt, atom->len);
    (void)err;
    inp->write_cursor = inp->write_buffer.count;
}
DRSH_INTERNAL void drsh_inp_move_down(DrshInput* inp){
    inp->hist_cursor++;
    inp->needs_redisplay = 1;
    if(inp->hist_cursor >= inp->hist_buffer.count/sizeof(const DrshAtom*)){
        inp->hist_cursor = inp->hist_buffer.count/sizeof(const DrshAtom*);
        drsh_gb_clear(&inp->write_buffer);
        inp->write_cursor = 0;
        return;
    }
    const DrshAtom* atom = ((const DrshAtom**)inp->hist_buffer.data)[inp->hist_cursor];
    drsh_gb_clear(&inp->write_buffer);
    DrshEC err = drsh_gb_append_(&inp->write_buffer, atom->txt, atom->len);
    (void)err;
    inp->write_cursor = inp->write_buffer.count;
}
DRSH_INTERNAL void drsh_end_tab_completion(DrshInput* inp){
    inp->tab_completion = 0;
    drsh_gb_clear(&inp->tab_completions);
}
DRSH_INTERNAL void drsh_parse_completable_token(const DrshReadBuffer rb, DrshStringView* tok, DrshStringView* dirname, DrshStringView* basename, _Bool backslash_is_sep){
    if(!rb.length) return;
    const char* end = (const char*)rb.ptr + rb.length;
    const char* begin = (const char*) rb.ptr;
    const char* slash = NULL;
    const char* p = end;
    do {
        --p;
        if(*p == ' '){
            if(p != begin && p[-1] == '\\')
                continue;
            p++;
            break;
        }
        if(!slash){
            if(*p == '/'){
                slash = p;
                continue;
            }
            // I think this is wrong for odd numbers of backslashes, but
            // how is that parsed normally??
            // TODO: tests
            if(backslash_is_sep && *p == '\\'){
                if(p != begin && p[-1] == '\\')
                    continue;
                slash = p;
                continue;
            }
        }
    }
    while(p != begin);

    tok->txt = p;
    tok->length = end - p;
    if(slash){
        basename->txt = slash+1;
        basename->length = end - (slash+1);
        dirname->txt = p;
        dirname->length = slash - p + 1;
    }
    else {
        *basename = *tok;
    }

}
typedef struct DrshWord DrshWord;
struct DrshWord {
    const DrshAtom* a;
    size_t distance;
    size_t idistance;
    size_t prefix_match;
    size_t iprefix_match;
};
DRSH_INTERNAL int word_cmp(const void* a, const void* b){
    const DrshWord *l = a, *r = b;
    if(l->prefix_match > r->prefix_match) return -1;
    if(l->prefix_match < r->prefix_match) return 1;
    if(l->iprefix_match > r->iprefix_match) return -1;
    if(l->iprefix_match < r->iprefix_match) return 1;
    if(l->distance < r->distance) return -1;
    if(l->distance > r->distance) return 1;
    if(l->idistance < r->idistance) return -1;
    if(l->idistance > r->idistance) return 1;
    if(l->a->txt[0] != '.' && r->a->txt[0] == '.') return -1;
    if(l->a->txt[0] == '.' && r->a->txt[0] != '.') return 1;
    return strcmp(l->a->txt, r->a->txt);
}

DRSH_INTERNAL void drsh_make_dirname(const DrshAtom*_Nullable pwd, DrshStringView dirname, DrshGrowBuffer* tmp){
    DrshEC err;
    drsh_gb_clear(tmp);
    if(dirname.length && drsh_path_is_abs(dirname, IS_WINDOWS)){
        err = drsh_gb_append_(tmp, dirname.txt, dirname.length);
        if(err) return;
    }
    else if(dirname.length){
        if(pwd){
            err = drsh_gb_append_(tmp, pwd->txt, pwd->len);
            if(err) return;
            err = drsh_gb_append_(tmp, "/", 1);
            if(err) return;
            err = drsh_gb_append_(tmp, dirname.txt, dirname.length);
            if(err) return;
        }
        else {
            err = drsh_gb_append_(tmp, dirname.txt, dirname.length);
            if(err) return;
        }
    }
    else if(pwd){
        err = drsh_gb_append_(tmp, pwd->txt, pwd->len);
        if(err) return;
    }
    else {
        err = drsh_gb_append_(tmp, ".", 1);
        if(err) return;
    }
}
DRSH_INTERNAL void drsh_readdir(const DrshAtom*_Nullable pwd, DrshStringView dirname, DrshGrowBuffer* out, DrshGrowBuffer* tmp, DrshAtomTable* at, _Bool dirs_only){
    DrshEC err;
    drsh_gb_clear(tmp);
    if(dirname.length && drsh_path_is_abs(dirname, IS_WINDOWS)){
        err = drsh_gb_append_(tmp, dirname.txt, dirname.length);
        if(err) return;
    }
    else if(dirname.length){
        if(pwd){
            err = drsh_gb_append_(tmp, pwd->txt, pwd->len);
            if(err) return;
            err = drsh_gb_append_(tmp, "/", 1);
            if(err) return;
            err = drsh_gb_append_(tmp, dirname.txt, dirname.length);
            if(err) return;
        }
        else {
            err = drsh_gb_append_(tmp, dirname.txt, dirname.length);
            if(err) return;
        }
    }
    else if(pwd){
        err = drsh_gb_append_(tmp, pwd->txt, pwd->len);
        if(err) return;
    }
    else {
        err = drsh_gb_append_(tmp, ".", 1);
        if(err) return;
    }
#ifdef _WIN32
    _Bool needs_slash = tmp->data[tmp->count-1] != '/' && tmp->data[tmp->count-1] != '\\';
    if(needs_slash){
        err = drsh_gb_append_(tmp, "\\*", 3);
        if(err) return;
    }
    err = drsh_gb_append_(tmp, "*", 2);
    if(err) return;
    DrshReadBuffer rb = drsh_gb_readable_buffer(tmp);
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(rb.ptr, &ffd);
    if(h == INVALID_HANDLE_VALUE){
        myperror("FindFirstFileA");
        printf("\r\nrb.ptr: '%.*s'\n", (int)rb.length, (const char*)rb.ptr);
        return;
    }
    drsh_gb_clear(tmp);
    do {
        const DrshAtom* a;
        size_t len = strlen(ffd.cFileName);
        if(len == 1 && ffd.cFileName[0] == '.') continue;
        if(len == 2 && ffd.cFileName[0] == '.' && ffd.cFileName[1]) continue;
        if(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
            err = drsh_gb_append_(tmp, ffd.cFileName, len);
            if(err) goto finally;
            err = drsh_gb_append_(tmp, "\\", 1);
            if(err) goto finally;
            DrshReadBuffer r = drsh_gb_readable_buffer(tmp);
            err = drsh_at_atomize(at, r.ptr, r.length, &a);
            drsh_gb_clear(tmp);
        }
        else if(dirs_only)
            continue;
        else
            err = drsh_at_atomize(at, ffd.cFileName, len, &a);
        DrshWord w = {
            a, 0, 0, 0, 0
        };
        err = drsh_gb_append_(out, &w, sizeof w);
        if(err) goto finally;
    }
    while (FindNextFile(h, &ffd) != 0);
    finally:
    FindClose(h);

#else
    err = drsh_gb_append_(tmp, "", 1);
    if(err) return;
    DrshReadBuffer rb = drsh_gb_readable_buffer(tmp);
    DIR* d = opendir(rb.ptr);
    drsh_gb_clear(tmp);
    if(!d) return;
    struct dirent* de;
    for(;(de = readdir(d));){
        const DrshAtom* a;
        size_t len;
        const char* name;
        #ifdef __APPLE__
        len = de->d_namlen;
        name = de->d_name;
        #else
        name = de->d_name;
        len = strlen(name);
        #endif
        if(len == 1 && name[0] == '.') continue;
        if(len == 2 && name[0] == '.' && name[1] == '.') continue;
        if(de->d_type == DT_DIR){
            drsh_gb_clear(tmp);
            err = drsh_gb_append_(tmp, name, len);
            if(err) goto finally;
            err = drsh_gb_append_(tmp, "/", 1);
            if(err) goto finally;
            DrshReadBuffer r = drsh_gb_readable_buffer(tmp);
            err = drsh_at_atomize(at, r.ptr, r.length, &a);
            drsh_gb_clear(tmp);
        }
        else if(de->d_type == DT_LNK){
            drsh_gb_clear(tmp);
            drsh_make_dirname(pwd, dirname, tmp);
            err = drsh_gb_append(tmp, "/", 1);
            if(err) continue;
            err = drsh_gb_append_(tmp, name, len+1);
            if(err) continue;
            struct stat s;
            int e = stat(tmp->data, &s);
            if(e) continue;
            drsh_gb_clear(tmp);
            if(S_ISDIR(s.st_mode)){
                err = drsh_gb_append_(tmp, name, len);
                if(err) goto finally;
                err = drsh_gb_append_(tmp, "/", 1);
                if(err) goto finally;
                DrshReadBuffer r = drsh_gb_readable_buffer(tmp);
                err = drsh_at_atomize(at, r.ptr, r.length, &a);
                drsh_gb_clear(tmp);
            }
            else if(dirs_only)
                continue;
            else {
                err = drsh_at_atomize(at, name, len, &a);
            }
        }
        else if(dirs_only)
            continue;
        else
            err = drsh_at_atomize(at, name, len, &a);
        if(err) goto finally;
        DrshWord w = {
            a, 0, 0, 0, 0
        };
        err = drsh_gb_append_(out, &w, sizeof w);
        if(err) goto finally;
    }
    finally:
    closedir(d);
#endif


}
DRSH_INTERNAL void drsh_tab_completion(DrshInput* inp, DrshEnvironment* env){
    if(!inp->tab_completion){
        _Bool dirs_only = 0;
        DrshReadBuffer rb = drsh_gb_readable_buffer(&inp->write_buffer);
        rb.length = inp->write_cursor;
        if(rb.length > 2 && memcmp(rb.ptr, "cd ", 3) == 0)
            dirs_only = 1;
        DrshStringView tok = {0}, dirname = {0}, basename = {0};
        // first, need to find the token we are completing (if any)
        drsh_parse_completable_token(rb, &tok, &dirname, &basename, IS_WINDOWS);
        drsh_gb_clear(&inp->tab_completions);
        {
            const DrshAtom* orig; DrshEC err = drsh_at_atomize(env->at, basename.txt, basename.length, &orig);
            DrshWord w = {orig, 0, 0, 0, 0};
            if(!err){
                err = drsh_gb_append_(&inp->tab_completions, &w, sizeof w);
                (void)err;
            }
        }
        const DrshAtom* pwd = drsh_env_get_env(env, env->at->special[ATOM_PWD]);
        drsh_readdir(pwd, dirname, &inp->tab_completions, &env->tmp, env->at, dirs_only);
        DRSH_SLICE(DrshWord) words = {inp->tab_completions.count/sizeof(DrshWord), (DrshWord*)inp->tab_completions.data};
        for(size_t i = 0; i < words.length; i++){
            DrshWord* w = &words.ptr[i];
            w->distance = basename.length?(size_t)byte_expansion_distance(w->a->txt, w->a->len, basename.txt, basename.length):0;
            w->idistance = basename.length?(size_t)byte_expansion_distance_icase(w->a->txt, w->a->len, basename.txt, basename.length):0;
            w->prefix_match = 0;
            if(basename.length <= w->a->len){
                w->prefix_match = memcmp(basename.txt, w->a->txt, basename.length) == 0;
                _Bool imatch = 1;
                for(size_t j = 0; j < basename.length; j++){
                    if((basename.txt[j] | 0x20) != (w->a->txt[j] | 0x20)){
                        imatch = 0;
                        break;
                    }
                }
                w->iprefix_match = imatch;
            }
        }
        qsort(words.ptr, words.length, sizeof *words.ptr, word_cmp);
        #if 0
        FILE* log = fopen("log.txt", "a");
        fprintf(log, "----\n");
        for(size_t i = 0; i < words.length; i++){
            DrshWord w = words.ptr[i];
            fprintf(log, "%zu) %zu %zu %zu %zu %s\n", i, w.prefix_match, w.iprefix_match, w.distance, w.idistance, w.a->txt);
        }
        fflush(log);
        fclose(log);
        #endif

        for(size_t i = words.length; i--; ){
            if(words.ptr[i].idistance  == (size_t)-1){
                inp->tab_completions.count -= sizeof *words.ptr;
            }
            else break;
        }
        inp->tab_completion_cursor = 0;
        inp->tab_completion = 1;
    }
    inp->tab_completion_cursor++;
    DrshReadBuffer atrb = drsh_gb_readable_buffer(&inp->tab_completions);
    DRSH_SLICE(const DrshWord) atoms = {atrb.length/sizeof(DrshWord), atrb.ptr};
    if(inp->tab_completion_cursor >= atoms.length)
        inp->tab_completion_cursor = 0;
    const DrshAtom* a = atoms.ptr[inp->tab_completion_cursor].a;
    const DrshAtom* prev = atoms.ptr[inp->tab_completion_cursor?inp->tab_completion_cursor-1:atoms.length-1].a;
    for(size_t i = 0; i < prev->len; i++){
        drsh_inp_del_left(inp);
    }
    for(size_t i = 0; i < a->len; i++){
        DrshEC err = drsh_inp_input_one(inp, a->txt[i]);
        (void)err;
    }
}
DRSH_INTERNAL void drsh_tab_completion_cancel(DrshInput* inp){
    if(!inp->tab_completion) return;
    DrshReadBuffer atrb = drsh_gb_readable_buffer(&inp->tab_completions);
    DRSH_SLICE(const DrshWord) atoms = {atrb.length/sizeof(const DrshWord), atrb.ptr};
    const DrshAtom* a = atoms.ptr[inp->tab_completion_cursor].a;
    for(size_t i = 0; i < a->len; i++){
        drsh_inp_del_left(inp);
    }
    a = atoms.ptr[0].a;
    for(size_t i = 0; i < a->len; i++){
        DrshEC err = drsh_inp_input_one(inp, a->txt[i]);
        (void)err;
    }
    drsh_end_tab_completion(inp);
}
DRSH_INTERNAL void drsh_tab_completion_prev(DrshInput* inp){
    if(!inp->tab_completion) return;
    inp->tab_completion_cursor--;
    DrshReadBuffer atrb = drsh_gb_readable_buffer(&inp->tab_completions);
    DRSH_SLICE(const DrshWord) atoms = {atrb.length/sizeof(const DrshWord), atrb.ptr};
    if(inp->tab_completion_cursor >= atoms.length)
        inp->tab_completion_cursor = atoms.length-1;
    const DrshAtom* a = atoms.ptr[inp->tab_completion_cursor].a;
    const DrshAtom* prev = atoms.ptr[inp->tab_completion_cursor< atoms.length-1?inp->tab_completion_cursor+1:0].a;
    for(size_t i = 0; i < prev->len; i++){
        drsh_inp_del_left(inp);
    }
    for(size_t i = 0; i < a->len; i++){
        DrshEC err = drsh_inp_input_one(inp, a->txt[i]);
        (void)err;
    }
}
DRSH_INTERNAL void drsh_inp_del_left(DrshInput* inp){
    if(!inp->write_cursor) return;
    if(inp->write_cursor == inp->write_buffer.count){
        inp->write_buffer.count--;
        inp->write_cursor--;
        inp->needs_redisplay = 1;
        return;
    }
    // XXX: unicode
    memremove(inp->write_cursor-1, inp->write_buffer.data, inp->write_buffer.count, 1);
    inp->needs_redisplay = 1;
    inp->write_cursor--;
    inp->write_buffer.count--;
}
DRSH_INTERNAL void drsh_inp_del_right(DrshInput* inp){
    if(inp->write_cursor == inp->write_buffer.count) return;
    if(inp->write_cursor == inp->write_buffer.count-1){
        inp->write_buffer.count--;
        inp->needs_redisplay = 1;
        return;
    }
    memremove(inp->write_cursor, inp->write_buffer.data, inp->write_buffer.count, 1);
    inp->needs_redisplay = 1;
    inp->write_buffer.count--;
}
DRSH_INTERNAL void drsh_inp_kill_end_of_line(DrshInput* inp){
    if(inp->write_buffer.count == inp->write_cursor) return;
    inp->write_buffer.count = inp->write_cursor;
    inp->needs_redisplay = 1;
}
DRSH_WARN_UNUSED
DRSH_INTERNAL DrshEC drsh_inp_input_one(DrshInput* inp, unsigned char c){
    DrshEC err = EC_OK;
    err = drsh_gb_ensure(&inp->write_buffer, sizeof c);
    if(err) return err;
    err = drsh_gb_insert(inp->write_cursor, &inp->write_buffer, &c, sizeof c);
    if(err) return err;
    inp->write_cursor += sizeof c;
    inp->needs_redisplay = 1;
    return EC_OK;
}

DRSH_INTERNAL
DrshEC
drsh_ts_write(DrshTermState*restrict ts, const void* p, size_t len){
    #ifdef _WIN32
        WriteFile(ts->out_fd, p, (DWORD)len, NULL, NULL);
    #else
        write(ts->out_fd, p, len);
    #endif
    return EC_OK;
}

DRSH_INTERNAL void drsh_inp_clear(DrshInput* inp){
    if(!inp->write_cursor && !inp->write_buffer.count)
        return;
    inp->write_buffer.count = 0;
    inp->write_cursor = 0;
    inp->needs_redisplay = 1;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_tokenize_line(const DrshReadBuffer *rb, DrshTokenized *t){
    drsh_gb_clear(&t->token_buffer);
    DrshEC err = EC_OK;
    char quoted = 0;
    _Bool backslash = 0;
    size_t length = rb->length;
    const char* txt = rb->ptr;
    const char* tok_begin = NULL;
    for(size_t i = 0; i < length; i++){
        char c = txt[i];
        if(!tok_begin){
            switch(c){
                case '\0':
                case ' ':
                case '\r':
                case '\t':
                case '\n':
                case '\f':
                    continue;
                case '"':
                case '\'':
                    quoted = c;
                    #ifdef __GNUC__
                    __attribute__((__fallthrough__));
                    #endif
                    // fallthrough
                default:
                    tok_begin = txt+i;
                    continue;
            }
        }
        if(backslash){
            backslash = 0;
            continue;
        }
        if(c == '\\'){
            assert(!backslash);
            backslash = 1;
            if(!tok_begin) tok_begin = txt+i;
            continue;
        }
        assert(tok_begin);
        if(c == quoted){
            quoted = 0;
            continue;
        }
        if(quoted) continue;
        switch(c){
            case '\0':
            case ' ':
            case '\r':
            case '\t':
            case '\n':
            case '\f':
                break;
            case '"':
            case '\'':
                quoted = c;
                // fallthrough
            default:
                continue;
        }

        assert(tok_begin);
        DrshToken tok = {.txt = tok_begin, .length = txt+i-tok_begin};
        err = drsh_gb_ensure2(&t->token_buffer, sizeof tok, 8*sizeof tok);
        if(err) return err;
        err = drsh_gb_append(&t->token_buffer, &tok, sizeof tok);
        if(err) return err;
        tok_begin = NULL;
    }
    if(tok_begin){
        DrshToken tok = {.txt = tok_begin, .length = txt+length-tok_begin};
        err = drsh_gb_ensure2(&t->token_buffer, sizeof tok, 8*sizeof tok);
        if(err) return err;
        err = drsh_gb_append(&t->token_buffer, &tok, sizeof tok);
        if(err) return err;
    }
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_canonicalize(DrshAtomTable* at, DrshGrowBuffer* tmp, const DrshToken* tok, const DrshAtom**atom, _Bool backslash_is_sep, DrshEnvironment* env){
    DrshEC err = EC_OK;
    const char* p = tok->txt;
    const char* end = tok->txt + tok->length;
    const char* dollar = NULL;
    char quoted = 0;
    _Bool backslash = 0;
    drsh_gb_clear(tmp);
    if(p != end && *p == '~' && env->home && env->home->len){
        if(p+1 == end || p[1] == '/' || (backslash_is_sep && p[1]=='\\')){
            p++;
            err = drsh_gb_append_(tmp, env->home->txt, env->home->len);
            if(err) return err;
        }
    }
    for(; p != end; p++){
        char c = *p;
        if(dollar){
            switch(c){
                case CASE_A_Z:
                case CASE_a_z:
                case CASE_0_9:
                case '_':
                    continue;
                default:{
                    const char* key = dollar+1;
                    ptrdiff_t len = p-key;
                    if(len > 0){
                        const DrshAtom* value = drsh_env_get_env2(env, key, len);
                        if(value){
                            err = drsh_gb_append_(tmp, value->txt, value->len);
                            if(err) return err;
                        }
                    }
                    dollar = NULL;
                }break;
            }
        }
        switch(c){
            case '$':
                if(!backslash){
                    dollar = p;
                    continue;
                }
                break;
            case '"':
                if(!backslash && quoted == '"'){
                    quoted = 0;
                    continue;
                }
                if(!backslash && !quoted){
                    quoted = '"';
                    continue;
                }
                break;
            case '\'':
                if(!backslash && quoted == '\''){
                    quoted = 0;
                    continue;
                }
                if(!backslash && !quoted){
                    quoted = '\'';
                    continue;
                }
                break;
            case '\\':
                if(!backslash){
                    backslash = 1;
                    continue;
                }
                break;
            default:
                break;
        }
        if(backslash){
            switch(c){
                case ' ':
                case '"':
                case '\'':
                    break;
                default:
                    err = drsh_gb_append_(tmp, "\\", 1);
                    if(err) return err;
                    break;
            }
        }
        backslash = 0;
        if(dollar) continue;
        err = drsh_gb_append_(tmp, &c, sizeof c);
        if(err) return err;
    }
    if(dollar){
        const char* key = dollar+1;
        ptrdiff_t len = end-key;
        if(len > 0){
            const DrshAtom* value = drsh_env_get_env2(env, key, len);
            if(value){
                err = drsh_gb_append_(tmp, value->txt, value->len);
                if(err) return err;
            }
        }
    }
    DrshReadBuffer rb = drsh_gb_readable_buffer(tmp);
    err = drsh_at_atomize(at, rb.ptr, rb.length, atom);
    return err;
}


DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_tokens_to_argv(DrshReadBuffer toks, DrshEnvironment* env, DrshAtomTable* at, DrshGrowBuffer* outbuff){
    DrshEC err = drsh_gb_ensure(outbuff, sizeof(char *)*(1+toks.length/sizeof(DrshToken)));
    if(err) return err;
    static DrshGrowBuffer tmp;
    for(EACH_RB(toks, const DrshToken, tok)){
        const DrshAtom* atom;
        err = drsh_canonicalize(at, &tmp, tok, &atom, IS_WINDOWS, env);
        if(err) return err;
        const char* p = atom->txt;
        // also does globbing need to happen here?
#if !defined(_WIN32)
    int flags = 0
        | GLOB_BRACE // FIXME: glob(3) doesn't do brace expansion correctly
        // | GLOB_TILDE
        | GLOB_NOCHECK
        ;
        glob_t g = {0};
        int e = glob(p, flags, NULL, &g);
        if(!e) for(size_t i = 0; i < g.gl_pathc; i++){
            const DrshAtom* a;
            err = drsh_at_atomize(at, g.gl_pathv[i], strlen(g.gl_pathv[i]), &a);
            if(err) return err;
            p = a->txt;
            err = drsh_gb_append_(outbuff, &p, sizeof p);
            if(err) return err;
        }
        globfree(&g);
#else
        // On Windows, programs are supposed to expand wildcards themselves.
        err = drsh_gb_append_(outbuff, &p, sizeof p);
        if(err) return err;
#endif
    }
    char* p = NULL;
    err = drsh_gb_append_(outbuff, &p, sizeof p);
    if(err) return err;
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_build_windows_command_line(DrshGrowBuffer* b, const char*const* argv){
    DrshEC err = EC_OK;
    for(const char*const* p = argv; *p; p++){
        const DrshAtom* a = drsh_unsafe_string_to_atom(*p);
        if(p == argv){
            err = drsh_gb_sprintf(b, "\"%.*s\"", (int)a->len, a->txt);
            if(err) return err;
            continue;
        }
        err = drsh_gb_append_(b, " ", 1);
        if(err) return err;
        if(memchr(a->txt, ' ', a->len) || memchr(a->txt, '\t', a->len)){
            err = drsh_gb_sprintf(b, "\"%.*s\"", (int)a->len, a->txt);
            if(err) return err;
            continue;
        }
        else {
            err = drsh_gb_append_(b, a->txt, a->len);
            if(err) return err;
            continue;
        }
    }
    err = drsh_gb_append_(b, "\0", 1);
    if(err) return err;
    return err;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_spawn_process_and_wait(DrshTermState* ts, DrshEnvironment* env, DrshGrowBuffer* tmp, const char*const* argv, _Bool report_time){
    (void)report_time;
    if(!argv[0]) return EC_VALUE_ERROR;
    void* envp = drsh_env_get_envp(env, IS_WINDOWS);
    DrshEC err;
    drsh_gb_clear(tmp);
    err = drsh_env_resolve_prog_path(env, tmp, drsh_unsafe_string_to_atom(argv[0]), IS_WINDOWS);
    if(err) {
        drsh_ts_printf(ts, "Unable to resolve program path for '%s'\r\n", argv[0]);
        return err;
    }
#ifdef _WIN32
    size_t cmd_cursor = tmp->count;
    err = drsh_build_windows_command_line(tmp, argv);
    if(err) return err;
    char* prog = tmp->data;
    char* cmd = tmp->data + cmd_cursor;
    STARTUPINFO startup = {
        .cb = sizeof startup,
        .dwFlags = STARTF_USESTDHANDLES,
        .hStdInput = ts->in_fd,
        .hStdOutput = ts->out_fd,
        .hStdError = GetStdHandle(STD_ERROR_HANDLE),
    };
    PROCESS_INFORMATION proc = {0};
    err = drsh_ts_orig(ts);
    if(env->debug){
        drsh_ts_printf(ts, "spawning '%s'\r\n", prog);
        drsh_ts_printf(ts, "cmd '%s'\r\n", cmd);
    }
    BOOL b = CreateProcessA(prog, cmd, NULL, NULL, TRUE, 0, envp, NULL, &startup, &proc);
    err = drsh_ts_unknown(ts);
    if(err) return err;
    if(!b){
        myperror("Create Process");
        return EC_VALUE_ERROR;
    }
    WaitForSingleObject(proc.hProcess, INFINITE);
    CloseHandle(proc.hProcess);
    CloseHandle(proc.hThread);
    return EC_OK;
#else
    int e;
    pid_t pid;
    // posix_spawn_file_actions_t actions_;
    // e = posix_spawn_file_actions_init(&actions_);
    // assert(!e);
    posix_spawn_file_actions_t* actions = NULL;
    posix_spawnattr_t* attrs = NULL;
    // restore term state to expected state
    err = drsh_ts_orig(ts);
    if(err) return err;
    if(env->debug){
        drsh_ts_printf(ts, "spawning '%s'\r\n", tmp->data);
        for(int i = 0;argv[i]; i++)
            drsh_ts_printf(ts, "argv[%d] '%s'\r\n", i, argv[i]);
    }
    #pragma GCC diagnostic ignored "-Wcast-qual"
    e = posix_spawn(&pid, tmp->data, actions, attrs, (char*const*)argv, envp);
    #pragma GCC diagnostic error "-Wcast-qual"
    // subprocess could've put us in any term state
    err = drsh_ts_unknown(ts);
    if(err) return err;
    if(e){
        drsh_ts_printf(ts, "\r%s\r\n", strerror(e));
    }
    else {
        int status;
        int options = 0;
        pid_t p;
        struct rusage usage = {0};
        for(;;){
            p = wait4(pid, &status, options, &usage);
            if(p == -1){
                if(errno == EINTR) continue;
            }
            break;
        }
        if(report_time){
            drsh_ts_printf(ts, "user   time: %lds%ldµs\r\n", usage.ru_utime.tv_sec, (long)usage.ru_utime.tv_usec);
            drsh_ts_printf(ts, "system time: %lds%ldµs\r\n", usage.ru_stime.tv_sec, (long)usage.ru_stime.tv_usec);
        }
    }
#endif
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_at_atomize(DrshAtomTable*restrict at, const char* restrict txt, size_t length, const DrshAtom**restrict out_atom){
    if(length >= UINT32_MAX) return EC_VALUE_ERROR;
    // if(1){
    if(at->count * 10/8 >= at->cap){
        // printf("grow table\r\n");
        // grow
        size_t old_cap = at->cap;
        size_t cap = old_cap?2*old_cap:4;
        size_t sz = cap*sizeof(DrshAtom*)+2*cap*sizeof(uint32_t);
        // printf("%zu/%zu\r\n", at->count, at->cap);
        // printf("%zu -> %zu\r\n", old_cap, cap);
        void* p = at->data?realloc(at->data, sz): malloc(sz);
        if(!p) return EC_OOM;
        DrshAtom** atoms = p;
        uint32_t* idxes = (uint32_t*)((char*)p + cap*sizeof *atoms);
        memset(idxes, 0, 2*cap * sizeof *idxes);
        size_t len = at->count;
        for(size_t i = 0; i < len; i++){
            // printf("%zu) %p -> '%s'\r\n", i, atoms[i], atoms[i]->txt);
            uint32_t hash = atoms[i]->hash;
            uint32_t idx = drsh_fast_reduce32(hash, cap);
            // printf("hash -> idx: %u -> %u\r\n", hash, idx);
            while(idxes[idx]){
                idx++;
                if(idx > 2*cap) idx = 0;
            }
            idxes[idx] = (uint32_t)i+1;
        }
        at->data = p;
        at->cap = cap;
    }
    size_t cap = at->cap;
    uint32_t hash = drsh_hash_align1(txt, length);
    if(!hash) hash = 1024;
    uint32_t idx = drsh_fast_reduce32(hash, cap);
    // printf("hash -> idx: %u -> %u\r\n", hash, idx);
    uint32_t i;
    DrshAtom** atoms = at->data;
    uint32_t* idxes = (uint32_t*)((char*)at->data + cap*sizeof *atoms);
    while((i = idxes[idx])){
        DrshAtom* atom = atoms[i-1];
        if(atom->hash == hash && atom->len == length && memcmp(atom->txt, txt, length) == 0){
            *out_atom = atom;
            return EC_OK;
        }
        idx++;
        if(idx > 2*cap) idx = 0;
    }
    assert(i == 0);
    DrshAtom *a = malloc(length+1+sizeof *a);
    char *b = malloc(length);
    if(!a || !b) {
        free(a);
        free(b);
        return EC_OOM;
    }

    i = (uint32_t)(at->count++);
    idxes[idx] = i+1;

    a->hash = hash;
    a->len = (uint32_t)length;
    memcpy(a->txt, txt, length);
    a->txt[length] = 0;
    a->iatom = NULL;
    atoms[i] = a;
    *out_atom = a;
    char* btxt = b;
    _Bool need_recursive_atomize = 0;
    for(const char *p = txt, *end = txt+length; p != end; p++){
        if((0x20|(unsigned)(unsigned char)*p) != (unsigned)(unsigned char)*p){
            need_recursive_atomize = 1;
        }
        *btxt++ = (char)(0x20|(unsigned)(unsigned char)*p);
    }
    if(need_recursive_atomize){
        DrshEC err = drsh_at_atomize(at, b, length, &a->iatom);
        if(err) return err;
    }
    else {
        a->iatom = a;
    }
    // printf("atomize: '%.*s' -> %p\r\n", (int)length, txt, a);
    return EC_OK;
}

DRSH_INTERNAL
void
drsh_dir_condense(DrshGrowBuffer* cwd, DrshGrowBuffer* tmp);

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_refresh_cwd(DrshEnvironment* env, _Bool backslash_is_sep){
    DrshGrowBuffer* tmp = &env->tmp;
    DrshGrowBuffer* cwd = &env->cwd;
    DrshEC err;
    drsh_gb_clear(tmp);
    err = drsh_gb_ensure(tmp, UINT16_MAX*2);
    if(err) return err;
    DrshWriteBuffer wb = drsh_gb_writable_buffer(tmp);
    size_t wd_len = 0;
    {
        #ifdef _WIN32
        wd_len = GetCurrentDirectoryA(wb.length, wb.ptr);
        #else
        char* wd = getcwd(wb.ptr, wb.length);
        if(wd) wd_len = strlen(wd);
        #endif
    }
    if(!wd_len){
        err = drsh_gb_append(tmp, "???", 3);
        assert(!err);
    }
    else {
        tmp->count += wd_len;
        err = drsh_env_set_env3(env, env->at->special[ATOM_PWD], wb.ptr, wd_len);
    }
    DrshWriteBuffer wd = {wd_len, wb.ptr};
    const DrshAtom*_Nullable home = env->home;
    drsh_gb_clear(cwd);
    if(home && home->len && wd.length >= home->len && memcmp(wd.ptr, home->txt, home->len)==0){
        if(home->len < wd.length){
            char c = ((char*)wd.ptr)[home->len];
            if(c != '/' && (backslash_is_sep && c != '\\')){
                goto not_match;
            }
        }
        err = drsh_gb_append_(cwd, "~", 1);
        if(err) return err;
        drsh_wb_shift(&wd, home->len);
        not_match:;
    }
    if(backslash_is_sep)
        for(size_t i = 0; i < wd.length; i++){
            if(((char*)wd.ptr)[i] == '\\') ((char*)wd.ptr)[i] = '/';
        }
    err = drsh_gb_append_(cwd, wd.ptr, wd.length);
    if(err) return err;
    drsh_dir_condense(cwd, tmp);
    return EC_OK;
}

DRSH_INTERNAL
void
drsh_dir_condense(DrshGrowBuffer* cwd, DrshGrowBuffer* tmp){
    drsh_gb_clear(tmp);
    size_t first_slash = 0;
    for(size_t i = 0; i < cwd->count; i++){
        char* p = cwd->data;
        if(p[i] == '/'){
            first_slash = i;
            break;
        }
    }
    size_t last_slash = 0;
    for(size_t i = cwd->count; i--;){
        char* p = cwd->data;
        if(p[i] == '/'){
            last_slash = i;
            break;
        }
    }
    if(!last_slash) return;
    _Bool want_write = 1;
    DrshEC err;
    if(first_slash){
        err = drsh_gb_append_(tmp, cwd->data, first_slash);
        if(err) return;
    }
    for(size_t i = first_slash; i < last_slash; i++){
        char* p = cwd->data;
        if(p[i] == '/'){
            want_write = 1;
            err = drsh_gb_append_(tmp, p+i, 1);
            if(err) return;
            continue;
        }
        if(want_write){
            err = drsh_gb_append_(tmp, p+i, 1);
            if(err) return;
            want_write = 0;
            continue;
        }
    }
    err = drsh_gb_append_(tmp, ((char*)cwd->data)+last_slash, cwd->count-last_slash);
    if(err) return;
    drsh_gb_clear(cwd);
    err = drsh_gb_append_(cwd, tmp->data, tmp->count);
    assert(!err);
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_refresh_prompt(DrshInput* input, DrshEnvironment* env){
    DrshEC err;
    DrshGrowBuffer* b = &input->prompt_buffer;
    drsh_gb_clear(b);
    size_t prompt_adj = 0;
    err = drsh_gb_append_(b, "\033[36m", -1+sizeof "\033[36m");
    if(err) return err;
    prompt_adj += 5;
    err = drsh_gb_ensure(b, 128);
    if(err) return err;
    {
        #ifdef _WIN32
        SYSTEMTIME tm;
        GetLocalTime(&tm);
        int hour = tm.wHour;
        if(hour >12) hour -= 12;
        if(hour == 0) hour = 12;
        err = drsh_gb_sprintf(b, "%02d/%02d %d:%02d%s ",
                (int)tm.wMonth, (int)tm.wDay,
                hour,
                (int)tm.wMinute, tm.wHour>11?"PM":"AM");
        if(err) return err;
        #else
        struct tm time_;
        time_t clock = time(NULL);
        localtime_r(&clock, &time_);
        DrshWriteBuffer wb = drsh_gb_writable_buffer(b);
        size_t n = strftime(wb.ptr, wb.length, "%m/%d %l:%M%p ", &time_);
        b->count += n;
        #endif
    }
    err = drsh_gb_append_(b, "\033[32m", -1+sizeof "\033[32m");
    if(err) return err;
    prompt_adj += 5;
    err = drsh_gb_append_(b, env->cwd.data, env->cwd.count);
    if(err) return err;
    err = drsh_gb_append_(b, "\033[38;5;248m> ", -1+sizeof "\033[38;5;248m> ");
    prompt_adj += 11;
    if(err) return err;
    err = drsh_gb_append_(b, "\033[0m", -1+sizeof "\033[0m");
    prompt_adj += 4;
    if(err) return err;
    input->prompt = drsh_gb_readable_buffer(b);
    input->prompt_visual_len = input->prompt.length-prompt_adj;
    return EC_OK;
}
DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_chdir(DrshEnvironment* env, const DrshArgv* rb){
    DrshEC err;
    DrshGrowBuffer* tmp = &env->tmp;
    drsh_gb_clear(tmp);
    size_t count = rb->length;
    const char*const* argv = rb->ptr;
    assert(count > 1);
    // pop trailing NULL
    count--;
    // skip "cd" token
    argv++; count--;
    if(count != 1) return EC_VALUE_ERROR;
    const DrshAtom* atom = drsh_unsafe_string_to_atom(argv[0]);
    err = drsh_gb_append_(tmp, atom->txt, atom->len);
    if(err) return err;
    err = drsh_gb_append_(tmp, "\0", 1);
    err = drsh_gb_append_(tmp, "\0", 1);
    if(err) return err;
    #ifdef _WIN32
        SetCurrentDirectoryA(tmp->data);
    #else
        chdir(tmp->data);
    #endif
    err = drsh_refresh_cwd(env, IS_WINDOWS);
    if(err) return err;
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_init(DrshEnvironment* env, DrshAtomTable* at, void* envp_, _Bool windows_style){
    #ifdef __APPLE__
    env->os_flavor = OS_APPLE;
    #elif defined(_WIN32)
    env->os_flavor = OS_WINDOWS;
    #elif defined(__linux__)
    env->os_flavor = OS_LINUX;
    #else
    env->os_flavor = OS_OTHER;
    #endif
    DrshEC err;
    DrshGrowBuffer* tmp = &env->tmp;
    drsh_gb_clear(tmp);
    err = drsh_gb_ensure(tmp, UINT16_MAX*2);
    if(err) return err;
    env->at = at;
    env->case_insensitive = windows_style;
    // env->case_insensitive = 1;//windows_style;
    if(windows_style){
        for(char* p = envp_;;){
            size_t len = strlen(p);
            if(!len) break;
            char* eq = strchr(p, '=');
            err = drsh_env_set_env4(env, p, eq-p, eq+1, p+len-eq-1);
            if(err) return err;
            p += len+1;
        }
    }
    else {
        char** envp = envp_;
        for(char**p = envp; *p; p++){
            const char* eq = strchr(*p, '=');
            if(!eq) continue;
            const DrshAtom *key, *value;
            err = drsh_at_atomize(at, *p, eq-*p, &key);
            if(err) return err;
            err = drsh_at_atomize(at, eq+1, strlen(eq+1), &value);
            if(err) return err;
            err = drsh_env_set_env(env, key, value);
            if(err) return err;
            assert(drsh_env_get_env(env, key) == value);
        }
    }
    env->home = drsh_env_get_env(env, env->at->special[ATOM_HOME]);
    return EC_OK;
}

DRSH_INTERNAL
void
drsh_env_sort_env(DrshEnvironment* env){
    _Bool icmp = env->case_insensitive;
    if(env->sorted) return;
    if(!env->count) return;
    qsort(env->data, env->count, 2*sizeof(DrshAtom*), icmp?drsh_atom_icmp:drsh_atom_cmp);
    const DrshAtom** atoms = env->data;
    size_t cap = env->cap;
    uint32_t* idxes = (uint32_t*)((char*)env->data + 2*cap*sizeof *atoms);
    memset(idxes, 0, 2*cap * sizeof *idxes);
    size_t len = env->count;
    for(size_t i = 0; i < len; i++){
        const DrshAtom* v = atoms[2*i];
        if(icmp) v = v->iatom;
        uint32_t hash = v->hash;
        uint32_t idx = drsh_fast_reduce32(hash, cap);
        while(idxes[idx]){
            idx++;
            if(idx > 2*cap) idx = 0;
        }
        idxes[idx] = (uint32_t)i+1;
    }
    env->sorted = 1;
}


DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_set_env(DrshEnvironment* env, const DrshAtom* key, const DrshAtom* value){
    _Bool case_insensitive = env->case_insensitive;
    const DrshAtom* lkey = case_insensitive?key->iatom:key;
    if(env->count * 10 /8 >= env->cap){
        size_t old_cap = env->cap;
        size_t cap = old_cap?2*old_cap:32;
        size_t sz = cap*sizeof(DrshAtom*)*2 + 2*cap*sizeof(uint32_t);
        void* p = env->data?realloc(env->data, sz):malloc(sz);
        if(!p) return EC_OOM;
        const DrshAtom** atoms = p;
        uint32_t* idxes = (uint32_t*)((char*)p + 2*cap*sizeof *atoms);
        memset(idxes, 0, 2*cap * sizeof *idxes);
        size_t len = env->count;
        for(size_t i = 0; i < len; i++){
            const DrshAtom* v = atoms[2*i];
            if(case_insensitive) v = v->iatom;
            uint32_t hash = v->hash;
            uint32_t idx = drsh_fast_reduce32(hash, cap);
            while(idxes[idx]){
                idx++;
                if(idx > 2*cap) idx = 0;
            }
            idxes[idx] = (uint32_t)i+1;
        }
        env->data = p;
        env->cap = cap;
    }
    size_t cap = env->cap;
    uint32_t hash = lkey->hash;
    uint32_t idx = drsh_fast_reduce32(hash, cap);
    uint32_t i;
    const DrshAtom** atoms = env->data;
    uint32_t* idxes = (uint32_t*)((char*)env->data + 2*cap*sizeof *atoms);
    while((i = idxes[idx])){
        const DrshAtom* atom = atoms[2*(i-1)];
        if(case_insensitive) atom = atom->iatom;
        if(atom == lkey){
            if(case_insensitive) atoms[2*(i-1)] = key;
            atoms[2*(i-1)+1] = value;
            return EC_OK;
        }
        idx++;
        if(idx > 2*cap) idx = 0;
    }
    assert(i == 0);
    i = (uint32_t)(env->count++);
    idxes[idx] = i+1;
    atoms[2*i] = key;
    atoms[2*i+1] = value;
    env->sorted = 0;
    return EC_OK;
}
DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_set_env3(DrshEnvironment* env, const DrshAtom* key, const char* value_txt, size_t value_len){
    const DrshAtom *value;
    DrshEC err;
    err = drsh_at_atomize(env->at, value_txt, value_len, &value);
    if(err) return err;
    return drsh_env_set_env(env, key, value);
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_set_env4(DrshEnvironment* env, const char* key_txt, size_t key_len, const char* value_txt, size_t value_len){
    const DrshAtom *key, *value;
    DrshEC err;
    err = drsh_at_atomize(env->at, key_txt, key_len, &key);
    if(err) return err;
    err = drsh_at_atomize(env->at, value_txt, value_len, &value);
    if(err) return err;
    return drsh_env_set_env(env, key, value);
}

DRSH_INTERNAL
const DrshAtom*_Nullable
drsh_env_get_env(DrshEnvironment* env, const DrshAtom* key){
    _Bool case_insensitive = env->case_insensitive;
    if(case_insensitive) key = key->iatom;
    size_t cap = env->cap;
    uint32_t hash = key->hash;
    uint32_t idx = drsh_fast_reduce32(hash, cap);
    uint32_t i;
    const DrshAtom** atoms = env->data;
    uint32_t* idxes = (uint32_t*)((char*)env->data + 2*cap*sizeof *atoms);
    while((i = idxes[idx])){
        const DrshAtom* atom = atoms[2*(i-1)];
        if(atom == key){
            return atoms[2*(i-1)+1];
        }
        idx++;
        if(idx > 2*cap) idx = 0;
    }
    if(case_insensitive){
        // slow and dumb
        for(size_t j = 0; j < env->count; j++){
            const DrshAtom* atom = atoms[2*j];
            if(case_insensitive) atom = atom->iatom;
            if(atom == key){
                return atoms[2*j+1];
            }
        }
    }
    return NULL;
}

DRSH_INTERNAL
const DrshAtom*_Nullable
drsh_env_get_env2(DrshEnvironment* env, const char* key, size_t len){
    const DrshAtom* key_atom;
    DrshEC err = drsh_at_atomize(env->at, key, len, &key_atom);
    if(err != EC_OK) return NULL;
    return drsh_env_get_env(env, key_atom);
}

DRSH_INTERNAL
void*_Nullable
drsh_env_get_envp(DrshEnvironment* env, _Bool windows_style){
    drsh_env_sort_env(env);
    DrshGrowBuffer* tmp = &env->tmp;
    drsh_gb_clear(tmp);
    DrshEC err;
    if(windows_style){
        for(size_t i = 0; i < env->count; i++){
            const DrshAtom** atoms = env->data;
            err = drsh_gb_append_(tmp, atoms[2*i]->txt, atoms[2*i]->len);
            if(err) return NULL;
            err = drsh_gb_append_(tmp, "=", 1);
            if(err) return NULL;
            err = drsh_gb_append_(tmp, atoms[2*i+1]->txt, atoms[2*i+1]->len);
            if(err) return NULL;
            err = drsh_gb_append_(tmp, "\0", 1);
            if(err) return NULL;
        }
        err = drsh_gb_append_(tmp, "\0", 1);
        if(err) return NULL;
    }
    else { // posix style
        for(size_t i = 0; i < env->count+1; i++){
            void* p = NULL;
            err = drsh_gb_append_(tmp, &p, sizeof p);
            if(err) return NULL;
        }
        for(size_t i = 0; i < env->count; i++){
            size_t off = tmp->count;
            const DrshAtom** atoms = env->data;
            err = drsh_gb_append_(tmp, atoms[2*i]->txt, atoms[2*i]->len);
            if(err) return NULL;
            err = drsh_gb_append_(tmp, "=", 1);
            if(err) return NULL;
            err = drsh_gb_append_(tmp, atoms[2*i+1]->txt, atoms[2*i+1]->len);
            if(err) return NULL;
            err = drsh_gb_append_(tmp, "\0", 1);
            if(err) return NULL;
            ((size_t*)tmp->data)[i] = off;
        }
        for(size_t i = 0; i < env->count; i++){
            size_t off = ((size_t*)tmp->data)[i];
            ((void**)tmp->data)[i] = (char*)tmp->data + off;
        }
    }
    return tmp->data;
}

DRSH_INTERNAL
_Bool
drsh_exists(const char* path){
    #ifdef _WIN32
    // printf("Trying %s\r\n", progpath);
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES;
    #else
    struct stat buf;
    int e = stat(path, &buf);
    return !e;
    #endif
}

//
// Arguments:
// ----------
// mem:
//   The buffer to search
//
// len:
//   The size of the buffer
//
// sep:
//   The character to search for in the buffer
//
// Returns:
// --------
// Pointer to the first found character or mem+len
// if the character is not found.
DRSH_INTERNAL
const char*
drsh_memchr(const char* mem, size_t len, char sep){
    const char* p = memchr(mem, sep, len);
    if(p) return p;
    return mem + len;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_resolve_prog_path(DrshEnvironment* env, DrshGrowBuffer* tmp, const DrshAtom* program, _Bool windows_style){
    DrshEC err;
    _Bool is_abs = 0;
    is_abs = drsh_path_is_abs(drsh_atom_sv(program), windows_style);
    _Bool has_dir = is_abs;
    if(!has_dir && memchr(program->txt, '/', program->len))
        has_dir = 1;
    if(!has_dir && windows_style && memchr(program->txt, '\\', program->len))
        has_dir = 1;
    if(has_dir){
        err = drsh_gb_append_(tmp, program->txt, program->len);
        if(err) return err;
        if(windows_style){
            DrshReadBuffer rb = drsh_gb_readable_buffer(tmp);
            const DrshAtom* pathexts = drsh_env_get_env(env, env->at->special[ATOM_PATHEXT]);
            size_t len = 4;
            const char* ext = ".exe";
            if(pathexts && pathexts->len){
                len = pathexts->len;
                ext = pathexts->txt;
            }
            for(const char *front = ext, *sep = drsh_memchr(ext, len, ';');;front=sep+1, sep = drsh_memchr(front, len-(front-ext), ';')){
                size_t ext_len = sep - front;
                if(drsh_rb_iendswith2(rb, front, ext_len)){
                    err = drsh_gb_append_(tmp, "\0", 1);
                    if(err) return err;
                    if(drsh_exists(tmp->data))
                        return EC_OK;
                    else
                        return EC_NOT_FOUND;
                }
                if(sep == ext+len) break;
            }
            for(const char *front = ext, *sep = drsh_memchr(ext, len, ';');;front=sep+1, sep = drsh_memchr(front, len-(front-ext), ';')){
                size_t ext_len = sep - front;
                err = drsh_gb_append_(tmp, front, ext_len);
                if(err) return err;
                err = drsh_gb_append_(tmp, "\0", 1);
                if(err) return err;
                if(drsh_exists(tmp->data))
                    return EC_OK;
                tmp->count -= 1 + ext_len;
                if(sep == ext+len) break;
            }
            return EC_NOT_FOUND;
        }
        err = drsh_gb_append_(tmp, "\0", 1);
        if(err) return err;
        return EC_OK;
    }
    const DrshAtom* path = drsh_env_get_env2(env, "PATH", 4);
    if(!path) return EC_NOT_FOUND;
    char path_separator = windows_style?';':':';
    const char* p = path->txt;
    for(;;){
        const char* s = strchr(p, path_separator);
        DrshStringView directory;
        if(!s){
            directory = (DrshStringView){
                .length = strlen(p),
                .txt = p,
            };
        }
        else {
            directory = (DrshStringView){
                .length = s-p,
                .txt = p,
            };
            p = s+1;
        }
        if(!directory.length){
            if(!s) break;
            continue;
        }
        drsh_gb_clear(tmp);
        err = drsh_gb_append_(tmp, directory.txt, directory.length);
        if(err) return err;
        char c = directory.txt[directory.length];
        _Bool append_slash = 1;
        if(windows_style && c == '\\')
            append_slash = 0;
        if(append_slash && c == '/')
            append_slash = 0;
        if(append_slash){
            err = drsh_gb_append_(tmp, "/", 1);
            if(err) return err;
        }
        err = drsh_gb_append_(tmp, program->txt, program->len);
        if(err) return err;
        if(windows_style){
            DrshReadBuffer rb = drsh_gb_readable_buffer(tmp);
            const DrshAtom* pathexts = drsh_env_get_env(env, env->at->special[ATOM_PATHEXT]);
            size_t len = 4;
            const char* ext = ".exe";
            if(pathexts && pathexts->len){
                len = pathexts->len;
                ext = pathexts->txt;
            }
            _Bool has_ext = 0;
            for(const char *front = ext, *sep = drsh_memchr(ext, len, ';');;front=sep+1, sep = drsh_memchr(front, len-(front-ext), ';')){
                size_t ext_len = sep - front;
                if(drsh_rb_iendswith2(rb, front, ext_len)){
                    has_ext = 1;
                    break;
                }
                if(sep == ext+len) break;
            }
            if(has_ext){
                err = drsh_gb_append_(tmp, "\0", 1);
                if(err) return err;
                if(drsh_exists(tmp->data))
                    return EC_OK;
            }
            else {
                for(const char *front = ext, *sep = drsh_memchr(ext, len, ';');;front=sep+1, sep = drsh_memchr(front, len-(front-ext), ';')){
                    size_t ext_len = sep - front;
                    err = drsh_gb_append_(tmp, front, ext_len);
                    if(err) return err;
                    err = drsh_gb_append_(tmp, "\0", 1);
                    if(err) return err;
                    if(drsh_exists(tmp->data))
                        return EC_OK;
                    tmp->count -= 1 + ext_len;
                    if(sep == ext+len) break;
                }
            }
        }
        else {
            err = drsh_gb_append_(tmp, "\0", 1);
            if(err) return err;
            char* progpath = tmp->data;
            if(drsh_exists(progpath))
                return EC_OK;
        }
        if(!s) break;
    }
    if(windows_style){
        const DrshAtom* dot = drsh_env_get_env(env, env->at->special[ATOM_PWD]);
        assert(dot);
        // look in '.'
        DrshStringView directory = {dot->len, dot->txt};
        // copy paste
        drsh_gb_clear(tmp);
        err = drsh_gb_append_(tmp, directory.txt, directory.length);
        if(err) return err;
        char c = directory.txt[directory.length];
        _Bool append_slash = 1;
        if(c == '\\')
            append_slash = 0;
        if(append_slash && c == '/')
            append_slash = 0;
        if(append_slash){
            err = drsh_gb_append_(tmp, "/", 1);
            if(err) return err;
        }
        err = drsh_gb_append_(tmp, program->txt, program->len);
        if(err) return err;
        DrshReadBuffer rb = drsh_gb_readable_buffer(tmp);
        const DrshAtom* pathexts = drsh_env_get_env(env, env->at->special[ATOM_PATHEXT]);
        size_t len = 4;
        const char* ext = ".exe";
        if(pathexts && pathexts->len){
            len = pathexts->len;
            ext = pathexts->txt;
        }
        _Bool has_ext = 0;
        for(const char *front = ext, *sep = drsh_memchr(ext, len, ';');;front=sep+1, sep = drsh_memchr(front, len-(front-ext), ';')){
            size_t ext_len = sep - front;
            if(drsh_rb_iendswith2(rb, front, ext_len)){
                has_ext = 1;
                break;
            }
            if(sep == ext+len) break;
        }
        if(has_ext){
            err = drsh_gb_append_(tmp, "\0", 1);
            if(err) return err;
            if(drsh_exists(tmp->data))
                return EC_OK;
        }
        else {
            for(const char *front = ext, *sep = drsh_memchr(ext, len, ';');;front=sep+1, sep = drsh_memchr(front, len-(front-ext), ';')){
                size_t ext_len = sep - front;
                err = drsh_gb_append_(tmp, front, ext_len);
                if(err) return err;
                err = drsh_gb_append_(tmp, "\0", 1);
                if(err) return err;
                if(drsh_exists(tmp->data))
                    return EC_OK;
                tmp->count -= 1 + ext_len;
                if(sep == ext+len) break;
            }
        }
    }
    return EC_NOT_FOUND;

}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_at_init(DrshAtomTable* at){
    DrshEC err;
    static const char*const tokens[] = {
        #define X(x) [ATOM_##x] = #x,
        ATOM_X(X)
        #undef X
        [ATOM_DOT] = ".",
    };
    static const size_t lens[] = {
        #define X(x) [ATOM_##x] = -1+sizeof #x,
        ATOM_X(X)
        #undef X
        [ATOM_DOT] = -1+sizeof ".",
    };
    for(int i = 0; i < ATOM_MAX; i++){
        err = drsh_at_atomize(at, tokens[i], lens[i], at->special+i);
        if(err) return err;
    }
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_hist_add(DrshInput* inp, const DrshAtom* atom){
    if(!atom->len) return EC_OK;
    DrshReadBuffer rb = drsh_gb_readable_buffer(&inp->hist_buffer);
    DRSH_SLICE(const DrshAtom* const) atoms = {rb.length/sizeof(const DrshAtom*), rb.ptr};
    inp->hist_cursor = atoms.length;
    if(atoms.length && atoms.ptr[atoms.length-1] == atom)
        return EC_OK;
    DrshEC err = drsh_gb_append_(&inp->hist_buffer, &atom, sizeof atom);
    if(err) return err;
    inp->hist_cursor = atoms.length+1;
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_hist_dump(const DrshInput* input, DrshEnvironment* env){
    DrshEC err = EC_OK;
    const DrshAtom* hist_path;
    err = drsh_env_get_history_path(env, &hist_path);
    if(err) return err;
    if(!hist_path || !hist_path->len) return EC_NOT_FOUND;
    DrshReadBuffer rb = drsh_gb_readable_buffer(&input->hist_buffer);
    DRSH_SLICE(const DrshAtom* const) atoms = {rb.length/sizeof(const DrshAtom*), rb.ptr};
    FileHandle fh;
    err = drsh_open_file_for_appending_with_mkdirs(hist_path->txt, hist_path->len, &fh);
    if(err) return err;
    for(size_t i = input->hist_start; i < atoms.length; i++){
        const DrshAtom* atom = atoms.ptr[i];
        err = drsh_append_to_file(fh, atom->txt, atom->len);
        (void)err;
    }
    err = drsh_close_file(fh);
    return err;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_refresh_size(DrshTermState* ts, DrshEnvironment* env){
    if(!ts->out_is_terminal) return EC_OK;
    {
    #ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        BOOL success = GetConsoleScreenBufferInfo(ts->out_fd, &csbi);
        if(!success) return EC_IO_ERROR;
        env->cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        env->lines = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    #else
        struct winsize w;
        int e = ioctl(ts->out_fd, TIOCGWINSZ, &w);
        if(e == -1) return EC_IO_ERROR;
        env->lines = w.ws_row;
        env->cols = w.ws_col;
    #endif
    }
    DrshEC err;
    char buff[16];
    int n;
    n = snprintf(buff, sizeof buff, "%d", env->lines);
    err = drsh_env_set_env3(env, env->at->special[ATOM_LINES], buff, n);
    if(err) return err;
    n = snprintf(buff, sizeof buff, "%d", env->cols);
    err = drsh_env_set_env3(env, env->at->special[ATOM_COLUMNS], buff, n);
    if(err) return err;
    return EC_OK;
}

DRSH_INTERNAL
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
DrshEC
drsh_ts_printf(DrshTermState*restrict ts, const char*restrict fmt, ...){
    va_list va;
    va_start(va, fmt);
    DrshEC err = drsh_ts_vprintf(ts, fmt, va);
    va_end(va);
    return err;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_ts_vprintf(DrshTermState*restrict ts, const char*restrict fmt, va_list va){
    DrshGrowBuffer* b = &ts->tmp;
    drsh_gb_clear(b);
    DrshEC err = drsh_gb_vsprintf(b, fmt, va);
    if(err) return err;
    DrshReadBuffer rb = drsh_gb_readable_buffer(b);
    return drsh_ts_write(ts, rb.ptr, rb.length);
}

DRSH_INTERNAL
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
DRSH_WARN_UNUSED
DrshEC
drsh_gb_sprintf(DrshGrowBuffer* gb, const char* fmt, ...){
    va_list va;
    va_start(va, fmt);
    DrshEC err = drsh_gb_vsprintf(gb, fmt, va);
    va_end(va);
    return err;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_gb_vsprintf(DrshGrowBuffer* b, const char* fmt, va_list va){
    DrshEC err;
    err = drsh_gb_ensure(b, UINT16_MAX*2);
    if(err) return err;
    for(;;){
        DrshWriteBuffer wb = drsh_gb_writable_buffer(b);
        va_list va2;
        va_copy(va2, va);
        int n = vsnprintf(wb.ptr, wb.length, fmt, va2);
        if(n < 0) return EC_ASSERTION_ERROR;
        if((size_t)n >= wb.length){
            err = drsh_gb_ensure(b, n+32);
            if(err) return err;
            va_end(va2);
            continue;
        }
        b->count += n;
        va_end(va2);
        break;
    }
    return err;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_get_history_path(DrshEnvironment* env, const DrshAtom** hist){
    {
        const DrshAtom* h = drsh_env_get_env(env, env->at->special[ATOM_DRSH_HISTORY]);
        if(h){
            *hist = h;
            return EC_OK;
        }
    }
    DrshEC err = EC_UNIMPLEMENTED_ERROR;
    DrshGrowBuffer* b = &env->tmp;
    drsh_gb_clear(b);
    OsFlavor os_flavor = env->os_flavor;
    if(os_flavor == OS_APPLE) {
        const DrshAtom*_Nullable home = env->home;
        if(!home || !home->len) return EC_NOT_FOUND;
        err = drsh_gb_append_(b, home->txt, home->len);
        if(err) return err;
        err = drsh_gb_append_(b, "/Library/Application Support", -1+sizeof "/Library/Application Support");
        if(err) return err;
    }
    else if(os_flavor == OS_WINDOWS){
        const DrshAtom*_Nullable localdata = drsh_env_get_env2(env, "LOCALAPPDATA", -1+sizeof "LOCALAPPDATA");
        if(!localdata || !localdata->len) return EC_NOT_FOUND;
        err = drsh_gb_append_(b, localdata->txt, localdata->len);
        if(err) return err;
    }
    else { // linux or other
        // use xdg
        const DrshAtom*_Nullable xdg = drsh_env_get_env2(env, "XDG_STATE_HOME", -1+sizeof "XDG_STATE_HOME");
        if(!xdg || !xdg->len) xdg = drsh_env_get_env2(env, "XDG_DATA_HOME", -1+sizeof "XDG_DATA_HOME");
        if(xdg && xdg->len){
            err = drsh_gb_append_(b, xdg->txt, xdg->len);
            if(err) return err;
        }
        else{
            const DrshAtom*_Nullable home = env->home;
            if(!home || !home->len) return EC_NOT_FOUND;
            err = drsh_gb_append_(b, home->txt, home->len);
            if(err) return err;
            err = drsh_gb_append_(b, "/.local/state", -1+sizeof "/.local/state");
            if(err) return err;
        }
    }
    err = drsh_gb_append_(b, "/drsh/drsh_history.txt", -1+sizeof "/drsh/drsh_history.txt");
    DrshReadBuffer rb = drsh_gb_readable_buffer(b);
    err = drsh_at_atomize(env->at, rb.ptr, rb.length, hist);
    if(err) return err;
    err = drsh_env_set_env(env, env->at->special[ATOM_DRSH_HISTORY], *hist);
    return err;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_env_get_config_path(DrshEnvironment* env, DrshGrowBuffer*b){
    DrshEC err = EC_UNIMPLEMENTED_ERROR;
    drsh_gb_clear(b);
    OsFlavor os_flavor = env->os_flavor;
    if(os_flavor == OS_APPLE) {
        const DrshAtom*_Nullable home = env->home;
        if(!home || !home->len) return EC_NOT_FOUND;
        err = drsh_gb_append_(b, home->txt, home->len);
        if(err) return err;
        err = drsh_gb_append_(b, "/Library/Application Support", -1+sizeof "/Library/Application Support");
        if(err) return err;
    }
    else if(os_flavor == OS_WINDOWS){
        const DrshAtom*_Nullable localdata = drsh_env_get_env2(env, "LOCALAPPDATA", -1+sizeof "LOCALAPPDATA");
        if(!localdata || !localdata->len) return EC_NOT_FOUND;
        err = drsh_gb_append_(b, localdata->txt, localdata->len);
        if(err) return err;
    }
    else { // linux or other
        // use xdg
        const DrshAtom*_Nullable xdg_config = drsh_env_get_env2(env, "XDG_CONFIG_HOME", -1+sizeof "XDG_CONFIG_HOME");
        if(xdg_config && xdg_config->len){
            err = drsh_gb_append_(b, xdg_config->txt, xdg_config->len);
            if(err) return err;
        }
        else{
            const DrshAtom*_Nullable home = env->home;
            if(!home || !home->len) return EC_NOT_FOUND;
            err = drsh_gb_append_(b, home->txt, home->len);
            if(err) return err;
            err = drsh_gb_append_(b, "/.config", -1+sizeof "/.config");
            if(err) return err;
        }
    }
    err = drsh_gb_append_(b, "/drsh/drsh_config.drsh", -1+sizeof "/drsh/drsh_config.drsh");
    return err;
}

DRSH_INTERNAL
DrshEC
drsh_env_set_shell_path(DrshEnvironment* env){
    DrshEC err = EC_UNIMPLEMENTED_ERROR;
    DrshGrowBuffer* b = &env->tmp;
    drsh_gb_clear(b);
#ifdef _WIN32
    err = drsh_gb_ensure(b, MAX_PATH);
    if(err) return err;
    DrshWriteBuffer wb = drsh_gb_writable_buffer(b);
    DWORD len = GetModuleFileNameA(NULL, wb.ptr, (DWORD)wb.length);
    if(!len) return EC_NOT_FOUND;
    b->count = len;
#elif defined(__APPLE__)
    err = drsh_gb_ensure(b, MAXPATHLEN);
    if(err) return err;
    DrshWriteBuffer wb = drsh_gb_writable_buffer(b);
    uint32_t bufsize = (uint32_t)wb.length;
    int e = _NSGetExecutablePath(wb.ptr, &bufsize);
    if(e < 0){
        err = drsh_gb_ensure(b, bufsize);
        if(err) return err;
        wb = drsh_gb_writable_buffer(b);
        bufsize = (uint32_t)wb.length;
        e = _NSGetExecutablePath(wb.ptr, &bufsize);
        if(e < 0) return EC_NOT_FOUND;
    }
    b->count = strlen(wb.ptr);
#elif defined(__linux__)
    err = drsh_gb_ensure(b, MAXPATHLEN);
    if(err) return err;
    DrshWriteBuffer wb = drsh_gb_writable_buffer(b);
    ssize_t ret = readlink("/proc/self/exe", wb.ptr, wb.length);
    if(ret < 0) return EC_NOT_FOUND;
    b->count = (size_t)ret;
#else
    return EC_UNIMPLEMENTED_ERROR;
#endif
    DrshReadBuffer rb = drsh_gb_readable_buffer(b);
    err = drsh_env_set_env3(env, env->at->special[ATOM_SHELL], rb.ptr, rb.length);
    return err;
}

DRSH_INTERNAL
DrshEC
drsh_env_increment_shlvl(DrshEnvironment* env){
    const DrshAtom* lvl_ = drsh_env_get_env(env, env->at->special[ATOM_SHLVL]);
    int lvl = 0;
    if(lvl_) lvl = atoi(lvl_->txt);
    lvl++;
    drsh_gb_clear(&env->tmp);
    DrshEC err = drsh_gb_sprintf(&env->tmp, "%d", lvl);
    if(err) return err;
    DrshReadBuffer rb = drsh_gb_readable_buffer(&env->tmp);
    err = drsh_env_set_env3(env, env->at->special[ATOM_SHLVL], rb.ptr, rb.length);
    return err;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_process_line(const DrshReadBuffer *input_line, DrshEnvironment* env, DrshAtomTable* at, DrshTokenized *tokens, DrshGrowBuffer* tok_argv, DrshTermState* ts, DrshGrowBuffer* tmp){
    if(input_line->length == 1){
        char c = ((const char*)input_line->ptr)[0];
        if(c == '\r' || c == '\n')
            return EC_OK;
    }
    DrshEC err;
    err = drsh_tokenize_line(input_line, tokens);
    if(err) return EC_OK;
    DrshReadBuffer toks = drsh_gb_readable_buffer(&tokens->token_buffer);
    drsh_gb_clear(tok_argv);
    err = drsh_tokens_to_argv(toks, env, at, tok_argv);
    if(err) return EC_OK;
    DrshArgv targv;
    {
        DrshReadBuffer targv_ = drsh_gb_readable_buffer(tok_argv);
        targv.ptr = targv_.ptr;
        targv.length = targv_.length/sizeof(const char*);
    }
    const DrshAtom* first = drsh_unsafe_string_to_atom(targv.ptr[0]);
    if(first == at->special[ATOM_cd]){
        err = drsh_chdir(env, &targv);
        (void)err;
        return EC_OK;
    }
    if(first == at->special[ATOM_echo]){
        for(size_t i = 1; i < targv.length; i++){
            const char* p = targv.ptr[i];
            if(p) {
                drsh_ts_printf(ts, "%s ", p);
            }
        }
        drsh_ts_write(ts, "\r\n", 2);
        return EC_OK;
    }
    if(first == at->special[ATOM_exit]){
        return EC_EXIT;
    }
    if(first == at->special[ATOM_pwd]){
        const DrshAtom* PWD = drsh_env_get_env(env, at->special[ATOM_pwd]);
        if(PWD) {
            drsh_ts_printf(ts, "%s\r\n", PWD->txt);
        }
        return EC_OK;
    }
    if(first == at->special[ATOM_set]){
        if(targv.length == 2){
            drsh_env_sort_env(env);
            const DrshAtom** atoms = env->data;
            size_t len = env->count;
            for(size_t i = 0; i < len; i++){
                const DrshAtom* key = atoms[i*2];
                const DrshAtom* value = atoms[i*2+1];
                if(IS_WINDOWS)
                    drsh_ts_printf(ts, "%s (%s)=%s\r\n", key->txt, key->iatom->txt, value->txt);
                else
                    drsh_ts_printf(ts, "%s=%s\r\n", key->txt, value->txt);
            }
        }
        if(targv.length != 4) return EC_OK;
        const DrshAtom* key = drsh_unsafe_string_to_atom(targv.ptr[1]);
        const DrshAtom* value = drsh_unsafe_string_to_atom(targv.ptr[2]);
        if(!key->len) return EC_OK;
        err = drsh_env_set_env(env, key, value);
        if(err) return EC_OK;
        return EC_OK;
    }
    if(first == at->special[ATOM_debug]){
        if(targv.length > 2){
            const DrshAtom* val = drsh_unsafe_string_to_atom(targv.ptr[1]);
            if(val == at->special[ATOM_on] || val == at->special[ATOM_true] || val == at->special[ATOM_1]){
                env->debug = 1;
            }
            else if(val == at->special[ATOM_off] || val == at->special[ATOM_false] || val == at->special[ATOM_0]){
                env->debug = 0;
            }
        }
        else
            drsh_ts_printf(ts, "debug = %s\r\n", env->debug?"true":"false");
        return EC_OK;
    }
    if(first == at->special[ATOM_source] || first == at->special[ATOM_DOT]){
        if(targv.length > 2){
            const DrshAtom* path = drsh_unsafe_string_to_atom(targv.ptr[1]);
            err = drsh_source_file(path, env, at, tokens, tok_argv, ts, tmp);
            return err;
        }
        return EC_OK;
    }
    if(first == at->special[ATOM_time]){
        if(targv.length > 2){
            err = drsh_spawn_process_and_wait(ts, env, tmp, targv.ptr+1, 1);
            if(err){
                drsh_ts_printf(ts, "error\r\n");
            }
        }
        return EC_OK;
    }
    err = drsh_spawn_process_and_wait(ts, env, tmp, targv.ptr, 0);
    if(err){
        drsh_ts_printf(ts, "error\r\n");
    }
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_source_file(const DrshAtom* path, DrshEnvironment* env, DrshAtomTable* at, DrshTokenized *tokens, DrshGrowBuffer* tok_argv, DrshTermState* ts, DrshGrowBuffer* tmp){
    DrshEC err = EC_OK;
    drsh_gb_clear(tmp);
    err = drsh_read_file(path->txt, tmp);
    if(!err){
        DrshReadBuffer txt = drsh_gb_readable_buffer(tmp);
        DrshReadBuffer line;
        for(;;){
            size_t len = drsh_rb_to_line(&txt, &line);
            if(!len) break;
            drsh_rb_shift(&txt, len);
            err = drsh_process_line(&line, env, at, tokens, tok_argv, ts, tmp);
            if(err == EC_EXIT)
                return EC_EXIT;
        }
    }
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_read_file(const char*restrict filepath, DrshGrowBuffer* outbuff){
#ifdef _WIN32
    // CreateFile has path as mutable for some reason,
    // not sure if it actually mutates the buffer.
    char path[8000];
    snprintf(path, sizeof path, "%s", filepath);
    HANDLE handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(handle == INVALID_HANDLE_VALUE) return EC_IO_ERROR;
    LARGE_INTEGER size;
    BOOL size_success = GetFileSizeEx(handle, &size);
    if(!size_success){
        CloseHandle(handle);
        return EC_IO_ERROR;
    }
    size_t nbytes = size.QuadPart;
    DrshEC err;
    err = drsh_gb_ensure(outbuff, nbytes);
    if(err){
        CloseHandle(handle);
        return err;
    }
    DrshWriteBuffer wb = drsh_gb_writable_buffer(outbuff);
    DWORD nread;
    BOOL read_success = ReadFile(handle, wb.ptr, nbytes, &nread, NULL);
    CloseHandle(handle);
    if(!read_success)
        return EC_IO_ERROR;
    assert(nread == nbytes);
    outbuff->count += nbytes;
    return EC_OK;
#else
    enum {flags = O_RDONLY};
    int fd = open(filepath, flags);
    if(fd < 0) return EC_IO_ERROR;
    struct stat s;
    int e = fstat(fd, &s);
    if(e == -1){
        close(fd);
        return EC_IO_ERROR;
    }
    if(!S_ISREG(s.st_mode)){
        // loop until eof
        return EC_UNIMPLEMENTED_ERROR;
    }
    else {
        off_t length = s.st_size;
        DrshEC err = drsh_gb_ensure(outbuff, length);
        if(err) {
            close(fd);
            return err;
        }
        DrshWriteBuffer wb = drsh_gb_writable_buffer(outbuff);
        ssize_t read_result = read(fd, wb.ptr, length);
        close(fd);
        if(read_result != length)
            return EC_IO_ERROR;
        outbuff->count += read_result;
        return EC_OK;
    }
#endif
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_open_file_for_appending_with_mkdirs(const char* path, size_t length, FileHandle* fh){
    (void)length;
    FileHandle fd;
#ifdef _WIN32
    char path_[8000];
    snprintf(path_, sizeof path_, "%s", path);
    fd = CreateFileA(path_, FILE_APPEND_DATA, FILE_SHARE_WRITE| FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if(fd == INVALID_HANDLE_VALUE)
        return EC_IO_ERROR;
#else
    fd = open(path, O_APPEND|O_CREAT|O_WRONLY, 0644);
    if(fd < 0) return EC_IO_ERROR;
#endif
    *fh = fd;
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_close_file(FileHandle fh){
#ifdef _WIN32
    CloseHandle(fh);
#else
    close(fh);
#endif
    return EC_OK;
}

DRSH_INTERNAL
DRSH_WARN_UNUSED
DrshEC
drsh_append_to_file(FileHandle fh, const char* txt, size_t length){
#ifdef _WIN32
    WriteFile(fh, txt, (DWORD)length, NULL, NULL);
    WriteFile(fh, "\n", (DWORD)1, NULL, NULL);
#else
    write(fh, txt, length);
    write(fh, "\n", 1);
#endif
    return EC_OK;
}
#ifdef __clang__
#pragma clang assume_nonnull end
#endif
