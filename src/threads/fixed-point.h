#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <inttypes.h>

typedef int32_t fixed_point;

#define FRACTIONAL_BITS 14

/* Fixed Point Conversion Constant*/
#define F (1<<FRACTIONAL_BITS)

/* Converts int32_t to fixed point form*/
#define int_to_fixed(n) ((n) * F)

/* Converts fixed point to int32_t by rounding towards zero*/
#define fixed_to_int_floor(x) ((x) / F)

/* Converts fixed point to int32_t by rounding to nearest*/
#define fixed_to_int_nearest(x) (((x) >= 0) ? (((x) + F / 2) / F) : (((x) - F / 2) / F))

/* Adds fixed point x to integer n, returning fixed point*/
#define addf(x,n) ((x) + (n) * F)

/* Integer n is subtracted from fixed point x, returning fixed point*/
#define subf(x,n) ((x) - (n) * F)

/* Multiplies two fixed point numbers, returns fixed point */
#define mulf(x,y) (((int64_t) (x)) * (y) / F)

/* Divides two fixed point numbers (x/y), returns fixed point*/
#define divf(x,y) (((int64_t) (x)) * F / (y))

#endif /* threads/fixed-point.h */