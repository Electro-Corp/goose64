/*
 * File:   fifo.h
 * Author: KRIK
 *
 * Created on 22 ������ 2011 �., 20:46
 */

#ifndef _FIFO_H
#define _FIFO_H

#include "ed64io_types.h"
int usbLoggerLog(const char* str);
int usbLoggerFlush();
void ed64Printf(const char* fmt, ...);

#endif /* _FIFO_H */
