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

#include "mhandle.h"
#include "si-program.h"


struct opencl_si_program_t *opencl_si_program_create(
		struct opencl_program_t *parent,
		struct opencl_si_device_t *device,
		void *binary,
		size_t length)
{
	struct opencl_si_program_t *program;

	/* Initialize */
	program = xcalloc(1, sizeof(struct opencl_si_program_t));

	/* Return */
	return program;
}

void opencl_si_program_free(struct opencl_si_program_t *program)
{
	free(program);
}

/* Return true is a binary file is a valid SI program binary. */
int opencl_si_program_valid_binary(
	void *binary,
	size_t length)
{
	Elf32_Ehdr *h = (Elf32_Ehdr *) binary;
	return h->e_machine == 0x3fd;
}