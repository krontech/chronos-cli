/*
 * File:   types.h
 * Author: David
 *
 * Created on January 14, 2014, 12:32 PM
 */

#ifndef TYPES_H
#define	TYPES_H

#ifdef	__cplusplus
extern "C" {
#endif

#define BOOL	unsigned char

#define TRUE (1==1)
#define FALSE (!TRUE)

typedef char			int8;
typedef int             int16;
typedef long			int32;
typedef unsigned char	uint8;
typedef unsigned int	uint16;
typedef unsigned long	uint32;

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define CLIP(x, min, max)	(((x) < (min)) ? (min) : ((x) > (max)) ? (max) : (x))

#ifdef	__cplusplus
}
#endif

#endif	/* TYPES_H */

