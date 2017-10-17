/* Copyright 2017 Kron Technologies. All Rights Reserved. */
#ifndef _CLI_UTILS_H
#define _CLI_UTILS_H

/* Bit hacking to extract a value from a bitmask and shift it down. */
static inline unsigned long
getbits(unsigned long value, unsigned long mask)
{
    if (!mask) {
        return 0;
    }
    else {
        unsigned long lsb = (~mask + 1) & mask;
        return (value & mask) / lsb;
    }
} /* getbits */

/* Bit hacking to shift a value up to the position specified by the mask.  */
static inline unsigned long
setbits(unsigned long value, unsigned long mask)
{
    if (!mask) {
        return 0;
    }
    else {
        unsigned long lsb = (~mask + 1) & mask;
        return (value * lsb) & mask;
    }
} /* setbits */

/* Used to store arrays of name/value pairs for UI helpers. */
struct enumval {
    const char *name;
    unsigned long value;
};


static inline const char *
enumval_name(const struct enumval *list, unsigned long value, const char *unknown)
{
    while (list->name) {
        if (list->value == value) {
            return list->name;
        }
        list++;
    }
    return unknown;
} /* enumval_name */

#endif /* _CLI_UTILS_H */
