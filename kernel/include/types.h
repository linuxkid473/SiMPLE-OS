#ifndef SIMPLE_TYPES_H
#define SIMPLE_TYPES_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

typedef uint32_t size_t;

/* POSIX types */
typedef int32_t  pid_t;
typedef int32_t  off_t;
typedef uint32_t mode_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int32_t  pgid_t;
typedef int32_t  sid_t;

#define NULL ((void*)0)

#endif
