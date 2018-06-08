/*
 * test_math.c
 *
 */

#include "kunit.h"
#include "lib/os/mathlib.h"
#include <stdio.h>
#include <math.h>
#include <fenv.h>

/*
 * Utility function to print the representation of a double
 * 
 */
void print_ieee(double x) {
    __ieee754_double_t* ieee = (__ieee754_double_t*) &x;
    printf("Double:            %f\n", x);
    printf("EXP:               %d\n", GET_EXP(x));
    printf("MANTISSA:          %llu\n", GET_MANTISSA(x));
    printf("ieee->mlow:        %d\n", ieee->mlow);
    printf("ieee->mhigh:       %d (%x)\n", ieee->mhigh, ieee->mhigh);    
    printf("ieee->sign:        %d\n", ieee->sign);
    printf("64 bit repr.       %llx\n", *((unsigned long long*) (&x)));
}

/*
 * Testcase 1: isinf
 */
int testcase1() {
    double value;
    value = 1.0 / 0.0;
    ASSERT(1 == __ctOS_isinf(value));
    value = 2.1;
    ASSERT(0 == __ctOS_isinf(value));
    ASSERT(__ctOS_isinf(__ctOS_inf()));
    return 0;
}

/*
 * Testcase 2: isnan
 */
int testcase2() {
    double value;
    value = sqrt(-1);
    ASSERT(1 == __ctOS_isnan(value));
    value = 2.1;
    ASSERT(0 == __ctOS_isnan(value));
    ASSERT(__ctOS_isnan(__ctOS_nan()));
    return 0;
}

/*
 * Testcase 3
 * Check exponent, mantissa and sign for an example
 */
int testcase3() {
    double x = 3.141;
    __ieee754_double_t* ieee = (__ieee754_double_t*) &x;
    unsigned long long* as64 = (unsigned long long *) &x;
    ASSERT(sizeof(unsigned long long) == 8);
    ASSERT(sizeof(unsigned long) == 4);
    ASSERT(sizeof(x) == 8);
    unsigned long int low_dword = (*as64) & 0xFFFFFFFF;
    unsigned long int high_dword = (*as64) >> 32;
    int exponent = ((high_dword >> 20) & 0x7ff) - 0x3FF;
    ASSERT(exponent == GET_EXP(x));
    return 0;
}

/*
 * Test ceil
 */
int testcase4() {
    double x;
    __ieee754_double_t* __ieee;
    /*
     * Try 0.5
     */
    ASSERT(__ctOS_ceil(0.5) == 1.0);
    /*
     * Try 1.0
     */
    ASSERT(__ctOS_ceil(1.0) == 1.0);
    /*
     * And 2.5, 1.5
     */
    ASSERT(__ctOS_ceil(1.5) == 2.0); 
    ASSERT(__ctOS_ceil(2.5) == 3.0);
    /*
     * Create a double where the high mantissa is completey filled, but the low mantissa is zero
     * This represents the decimal number 7.999996 (roughly) and binary 111.111111111111111111
     */
    __ieee = (__ieee754_double_t*) (&x);
    __ieee->sign = 0;
    __ieee->mlow = 0;
    __ieee->mhigh = 0xFFFFF;
    __ieee->exp = BIAS + 2;
    ASSERT(__ctOS_ceil(x) == 8.0);
    /*
     * Now do a double where the low mantissa has a bit set as well
     */
    __ieee->mlow = 0x80000000;
    ASSERT(__ctOS_ceil(x) == 8.0);
    /*
     * Finally do a negative number
     */
    __ieee->sign = 1;
    ASSERT(__ctOS_ceil(x) == -7.0);
    /*
     * and zero
     */
    ASSERT(0.0 == __ctOS_ceil(0.0));
    return 0;
}

/*
 * Testcase 5: test special cases for ceil:
 * 0.0
 * inf
 * nan
 */
int testcase5() {
    double inf = 1.0 / 0.0;
    double nan = sqrt(-1.0);
    ASSERT(__ctOS_isinf(inf));
    ASSERT(__ctOS_isinf(__ctOS_ceil(inf)));
    ASSERT(0.0 == __ctOS_ceil(0.0));
    ASSERT(__ctOS_isnan(__ctOS_ceil(nan)));
    return 0;
}

/*
 * Test floor
 */
int testcase6() {
    double x;
    __ieee754_double_t* __ieee;
    /*
     * Try 0.5
     */
    ASSERT(__ctOS_floor(0.5) == 0.0);
    /*
     * Try 1.0
     */
    ASSERT(__ctOS_floor(1.0) == 1.0);
    /*
     * And 2.5, 1.5
     */
    ASSERT(__ctOS_floor(1.5) == 1.0); 
    ASSERT(__ctOS_floor(2.5) == 2.0);
    /*
     * Create a double where the high mantissa is completey filled, but the low mantissa is zero
     * This represents the decimal number 7.999996 (roughly) and binary 111.111111111111111111
     */
    __ieee = (__ieee754_double_t*) (&x);
    __ieee->sign = 0;
    __ieee->mlow = 0;
    __ieee->mhigh = 0xFFFFF;
    __ieee->exp = BIAS + 2;
    ASSERT(__ctOS_floor(x) == 7.0);
    /*
     * Now do a double where the low mantissa has a bit set as well
     */
    __ieee->mlow = 0x80000000;
    ASSERT(__ctOS_floor(x) == 7.0);
    /*
     * Finally do a negative number
     */
    __ieee->sign = 1;
    ASSERT(__ctOS_floor(x) == -8.0);
    /*
     * and zero
     */
    ASSERT(0.0 == __ctOS_floor(0.0));
    return 0;
}

/*
 * Test binary logarithm - no reduction, i.e. the argument is
 * already in the range 1...2
 */
int testcase7() {
    double x = 1.0;
    double y1, y2;
    double error = 0.0;
    double errorat = 0;
    for (int i = 0; i < 999; i++) {
        y1 = __ctOS_log2(x);
        y2 = log2(x);
        if (fabs(y1 - y2) > error) {
            error = fabs(y1-y2);
            errorat = x;
        }
        x = x + 0.001;
    }
    ASSERT(error < 1e-15);
    return 0;
}

/*
 * Test binary logarithm - reduction
 */
int testcase8() {
    double x = 5.0;
    double y1, y2;
    double error = 0.0;
    double errorat = 0;
    for (int i = 0; i < 999; i++) {
        y1 = __ctOS_log2(x);
        y2 = log2(x);
        if (fabs(y1 - y2) > error) {
            error = fabs(y1-y2);
            errorat = x;
        }
        x = x + 0.001;
    }
    ASSERT(error < 1e-15);
    return 0;
}

/*
 * Test binary logarithm - special cases
 */
int testcase9() {
    ASSERT(0 == __ctOS_log2(1.0));
    ASSERT(__ctOS_inf(__ctOS_log2(0.0)));
    ASSERT(__ctOS_inf(log2(0.0)));
    ASSERT(__ctOS_isnan(__ctOS_log2(-1.0)));
    ASSERT(__ctOS_isnan(__ctOS_log2(__ctOS_nan())));
    ASSERT(__ctOS_isinf(__ctOS_log2(__ctOS_inf())));
    ASSERT(__ctOS_isnan(__ctOS_log2(-1.0*__ctOS_inf())));
    return 0;
}


int main() {
    // print_ieee(1.5);
    // print_ieee(sqrt(-1.0));
    INIT;
    RUN_CASE(1);
    RUN_CASE(2);
    RUN_CASE(3);
    RUN_CASE(4);
    RUN_CASE(5);
    RUN_CASE(6);
    RUN_CASE(7);
    RUN_CASE(8);
    RUN_CASE(9);
    END;
}

