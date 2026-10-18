/*	$NetBSD$	*/

/*
 * long double (TFmode) entry points for soft-float little-endian MIPS.
 * GCC returns TFmode in $2/$4; the SoftFloat C functions return a struct in
 * $2/$3, so call them through a union and let the compiler place the value.
 */
#include "softfloat-for-gcc.h"
#include "milieu.h"
#include "softfloat.h"

#ifdef MIPS_TF_ABI_SHIM

union tfconv {
	long double ld;
	float128 f;
};

float128 _softfloat128_add(float128, float128);
float128 _softfloat128_sub(float128, float128);
float128 _softfloat128_mul(float128, float128);
float128 _softfloat128_div(float128, float128);
float128 _softfloat128_neg(float128);
float128 _softfloat128_from_int32(int32);
float128 _softfloat128_from_int64(int64);
float128 _softfloat128_from_float32(float32);
float128 _softfloat128_from_float64(float64);

#define BINOP(name, real)						\
long double name(long double, long double);				\
long double								\
name(long double a, long double b)					\
{									\
	union tfconv x, y, r;						\
	x.ld = a;							\
	y.ld = b;							\
	r.f = real(x.f, y.f);						\
	return r.ld;							\
}

#define CONV(name, real, type)						\
long double name(type);							\
long double								\
name(type a)								\
{									\
	union tfconv r;							\
	r.f = real(a);							\
	return r.ld;							\
}

BINOP(__addtf3, _softfloat128_add)
BINOP(__subtf3, _softfloat128_sub)
BINOP(__multf3, _softfloat128_mul)
BINOP(__divtf3, _softfloat128_div)

long double __negtf2(long double);
long double
__negtf2(long double a)
{
	union tfconv x, r;

	x.ld = a;
	r.f = _softfloat128_neg(x.f);
	return r.ld;
}

CONV(__floatsitf, _softfloat128_from_int32, int32)
CONV(__floatditf, _softfloat128_from_int64, int64)
CONV(__extendsftf2, _softfloat128_from_float32, float32)
CONV(__extenddftf2, _softfloat128_from_float64, float64)

#endif /* MIPS_TF_ABI_SHIM */
