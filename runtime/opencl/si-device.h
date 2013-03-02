/*
 *  Multi2Sim
 *  Copyright (C) 2012  Rafael Ubal (ubal@ece.neu.edu)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef RUNTIME_OPENCL_SI_DEVICE_H
#define RUNTIME_OPENCL_SI_DEVICE_H

#include "opencl.h"


struct opencl_si_device_t
{
	/* Parent generic device object */
	struct opencl_device_t *parent;
};



struct opencl_si_device_t *opencl_si_device_create(struct opencl_device_t *parent);
void opencl_si_device_free(struct opencl_si_device_t *device);

void *opencl_si_device_mem_alloc(size_t size);
void opencl_si_device_mem_free(void *ptr);
void opencl_si_device_mem_read(void *host_ptr, void *device_ptr, size_t size);
void opencl_si_device_mem_write(void *device_ptr, void *host_ptr, size_t size);
void opencl_si_device_mem_copy(void *device_dest_ptr, void *device_src_ptr, size_t size);

#endif
