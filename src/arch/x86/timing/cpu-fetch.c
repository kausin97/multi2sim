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

#include <arch/x86/emu/context.h>
#include <arch/x86/emu/regs.h>
#include <mem-system/mem-system.h>

#include "bpred.h"
#include "cpu.h"
#include "event-queue.h"
#include "fetch-queue.h"
#include "reg-file.h"
#include "trace-cache.h"
#include "uop.h"


static int x86_cpu_can_fetch(int core, int thread)
{
	struct x86_ctx_t *ctx = X86_THREAD.ctx;

	unsigned int phy_addr;
	unsigned int block;

	/* Context must be running */
	if (!ctx || !x86_ctx_get_status(ctx, x86_ctx_running))
		return 0;
	
	/* Fetch stalled or context evict signal activated */
	if (X86_THREAD.fetch_stall_until >= x86_cpu->cycle || ctx->dealloc_signal)
		return 0;
	
	/* Fetch queue must have not exceeded the limit of stored bytes
	 * to be able to store new macro-instructions. */
	if (X86_THREAD.fetchq_occ >= x86_fetch_queue_size)
		return 0;
	
	/* If the next fetch address belongs to a new block, cache system
	 * must be accessible to read it. */
	block = X86_THREAD.fetch_neip & ~(X86_THREAD.inst_mod->block_size - 1);
	if (block != X86_THREAD.fetch_block)
	{
		phy_addr = mmu_translate(X86_THREAD.ctx->address_space_index,
			X86_THREAD.fetch_neip);
		if (!mod_can_access(X86_THREAD.inst_mod, phy_addr))
			return 0;
	}
	
	/* We can fetch */
	return 1;
}


/* Execute in the simulation kernel a macro-instruction and create uops.
 * If any of the uops is a control uop, this uop will be the return value of
 * the function. Otherwise, the first decoded uop is returned. */
static struct x86_uop_t *x86_cpu_fetch_inst(int core, int thread, int fetch_trace_cache)
{
	struct x86_ctx_t *ctx = X86_THREAD.ctx;

	struct x86_uop_t *uop;
	struct x86_uop_t *ret_uop;

	struct x86_uinst_t *uinst;
	int uinst_count;
	int uinst_index;

	/* Functional simulation */
	X86_THREAD.fetch_eip = X86_THREAD.fetch_neip;
	x86_ctx_set_eip(ctx, X86_THREAD.fetch_eip);
	x86_ctx_execute(ctx);
	X86_THREAD.fetch_neip = X86_THREAD.fetch_eip + ctx->inst.size;

	/* Micro-instructions created by the x86 instructions can be found now
	 * in 'x86_uinst_list'. */
	uinst_count = list_count(x86_uinst_list);
	uinst_index = 0;
	ret_uop = NULL;
	while (list_count(x86_uinst_list))
	{
		/* Get uinst from head of list */
		uinst = list_remove_at(x86_uinst_list, 0);

		/* Create uop */
		uop = x86_uop_create();
		uop->uinst = uinst;
		assert(uinst->opcode > 0 && uinst->opcode < x86_uinst_opcode_count);
		uop->flags = x86_uinst_info[uinst->opcode].flags;
		uop->id = x86_cpu->uop_id_counter++;
		uop->id_in_core = X86_CORE.uop_id_counter++;

		uop->ctx = ctx;
		uop->core = core;
		uop->thread = thread;

		uop->mop_count = uinst_count;
		uop->mop_size = ctx->inst.size;
		uop->mop_id = uop->id - uinst_index;
		uop->mop_index = uinst_index;

		uop->eip = X86_THREAD.fetch_eip;
		uop->in_fetch_queue = 1;
		uop->fetch_trace_cache = fetch_trace_cache;
		uop->specmode = x86_ctx_get_status(ctx, x86_ctx_spec_mode);
		uop->fetch_address = X86_THREAD.fetch_address;
		uop->fetch_access = X86_THREAD.fetch_access;
		uop->neip = ctx->regs->eip;
		uop->pred_neip = X86_THREAD.fetch_neip;
		uop->target_neip = ctx->target_eip;

		/* Process uop dependences and classify them in integer, floating-point,
		 * flags, etc. */
		x86_reg_file_count_deps(uop);

		/* Calculate physical address of a memory access */
		if (uop->flags & X86_UINST_MEM)
			uop->phy_addr = mmu_translate(X86_THREAD.ctx->address_space_index,
				uinst->address);

		/* Trace */
		if (x86_tracing())
		{
			char str[MAX_STRING_SIZE];
			char inst_name[MAX_STRING_SIZE];
			char uinst_name[MAX_STRING_SIZE];

			char *str_ptr;

			int str_size;

			str_ptr = str;
			str_size = sizeof str;

			/* Command */
			str_printf(&str_ptr, &str_size, "x86.new_inst id=%lld core=%d",
				uop->id_in_core, uop->core);

			/* Speculative mode */
			if (uop->specmode)
				str_printf(&str_ptr, &str_size, " spec=\"t\"");

			/* Macro-instruction name */
			if (!uinst_index)
			{
				x86_inst_dump_buf(&ctx->inst, inst_name, sizeof inst_name);
				str_printf(&str_ptr, &str_size, " asm=\"%s\"", inst_name);
			}

			/* Rest */
			x86_uinst_dump_buf(uinst, uinst_name, sizeof uinst_name);
			str_printf(&str_ptr, &str_size, " uasm=\"%s\" stg=\"fe\"", uinst_name);

			/* Dump */
			x86_trace("%s\n", str);
		}

		/* Select as returned uop */
		if (!ret_uop || (uop->flags & X86_UINST_CTRL))
			ret_uop = uop;

		/* Insert into fetch queue */
		list_add(X86_THREAD.fetch_queue, uop);
		x86_cpu->fetched++;
		X86_THREAD.fetched++;
		if (fetch_trace_cache)
			X86_THREAD.trace_cache_queue_occ++;

		/* Next uinst */
		uinst_index++;
	}

	/* Increase fetch queue occupancy if instruction does not come from
	 * trace cache, and return. */
	if (ret_uop && !fetch_trace_cache)
		X86_THREAD.fetchq_occ += ret_uop->mop_size;
	return ret_uop;
}


/* Try to fetch instruction from trace cache.
 * Return true if there was a hit and fetching succeeded. */
static int x86_cpu_fetch_thread_trace_cache(int core, int thread)
{
	struct x86_uop_t *uop;

	int mpred;
	int hit;
	int mop_count;
	int i;

	unsigned int eip_branch;  /* next branch address */
	unsigned int *mop_array;
	unsigned int neip;

	/* No trace cache, no space in the trace cache queue. */
	if (!x86_trace_cache_present)
		return 0;
	if (X86_THREAD.trace_cache_queue_occ >= x86_trace_cache_queue_size)
		return 0;
	
	/* Access BTB, branch predictor, and trace cache */
	eip_branch = x86_bpred_btb_next_branch(X86_THREAD.bpred,
		X86_THREAD.fetch_neip, X86_THREAD.inst_mod->block_size);
	mpred = eip_branch ? x86_bpred_lookup_multiple(X86_THREAD.bpred,
		eip_branch, x86_trace_cache_branch_max) : 0;
	hit = x86_trace_cache_lookup(X86_THREAD.trace_cache, X86_THREAD.fetch_neip, mpred,
		&mop_count, &mop_array, &neip);
	if (!hit)
		return 0;
	
	/* Fetch instruction in trace cache line. */
	for (i = 0; i < mop_count; i++)
	{
		/* If instruction caused context to suspend or finish */
		if (!x86_ctx_get_status(X86_THREAD.ctx, x86_ctx_running))
			break;
		
		/* Insert decoded uops into the trace cache queue. In the simulation,
		 * the uop is inserted into the fetch queue, but its occupancy is not
		 * increased. */
		X86_THREAD.fetch_neip = mop_array[i];
		uop = x86_cpu_fetch_inst(core, thread, 1);
		if (!uop)  /* no uop was produced by this macroinst */
			continue;

		/* If instruction is a branch, access branch predictor just in order
		 * to have the necessary information to update it at commit. */
		if (uop->flags & X86_UINST_CTRL)
		{
			x86_bpred_lookup(X86_THREAD.bpred, uop);
			uop->pred_neip = i == mop_count - 1 ? neip :
				mop_array[i + 1];
		}
	}

	/* Set next fetch address as returned by the trace cache, and exit. */
	X86_THREAD.fetch_neip = neip;
	return 1;
}


static void x86_cpu_fetch_thread(int core, int thread)
{
	struct x86_ctx_t *ctx = X86_THREAD.ctx;
	struct x86_uop_t *uop;

	unsigned int phy_addr;
	unsigned int block;
	unsigned int target;

	int taken;

	/* Try to fetch from trace cache first */
	if (x86_cpu_fetch_thread_trace_cache(core, thread))
		return;
	
	/* If new block to fetch is not the same as the previously fetched (and stored)
	 * block, access the instruction cache. */
	block = X86_THREAD.fetch_neip & ~(X86_THREAD.inst_mod->block_size - 1);
	if (block != X86_THREAD.fetch_block)
	{
		phy_addr = mmu_translate(X86_THREAD.ctx->address_space_index, X86_THREAD.fetch_neip);
		X86_THREAD.fetch_block = block;
		X86_THREAD.fetch_address = phy_addr;
		X86_THREAD.fetch_access = mod_access(X86_THREAD.inst_mod, 
			mod_access_load, phy_addr, NULL, NULL, NULL);
		X86_THREAD.btb_reads++;

		/* MMU statistics */
		if (*mmu_report_file_name)
			mmu_access_page(phy_addr, mmu_access_execute);
	}

	/* Fetch all instructions within the block up to the first predict-taken branch. */
	while ((X86_THREAD.fetch_neip & ~(X86_THREAD.inst_mod->block_size - 1)) == block)
	{
		/* If instruction caused context to suspend or finish */
		if (!x86_ctx_get_status(ctx, x86_ctx_running))
			break;
	
		/* If fetch queue full, stop fetching */
		if (X86_THREAD.fetchq_occ >= x86_fetch_queue_size)
			break;
		
		/* Insert macro-instruction into the fetch queue. Since the macro-instruction
		 * information is only available at this point, we use it to decode
		 * instruction now and insert uops into the fetch queue. However, the
		 * fetch queue occupancy is increased with the macro-instruction size. */
		uop = x86_cpu_fetch_inst(core, thread, 0);
		if (!ctx->inst.size)  /* x86_isa_inst invalid - no forward progress in loop */
			break;
		if (!uop)  /* no uop was produced by this macro-instruction */
			continue;

		/* Instruction detected as branches by the BTB are checked for branch
		 * direction in the branch predictor. If they are predicted taken,
		 * stop fetching from this block and set new fetch address. */
		if (uop->flags & X86_UINST_CTRL)
		{
			target = x86_bpred_btb_lookup(X86_THREAD.bpred, uop);
			taken = target && x86_bpred_lookup(X86_THREAD.bpred, uop);
			if (taken)
			{
				X86_THREAD.fetch_neip = target;
				uop->pred_neip = target;
				break;
			}
		}
	}
}


static void x86_cpu_fetch_core(int core)
{
	int thread;

	switch (x86_cpu_fetch_kind)
	{

	case x86_cpu_fetch_kind_shared:
	{
		/* Fetch from all threads */
		X86_THREAD_FOR_EACH
		{
			if (x86_cpu_can_fetch(core, thread))
				x86_cpu_fetch_thread(core, thread);
		}
		break;
	}

	case x86_cpu_fetch_kind_timeslice:
	{
		/* Round-robin fetch */
		X86_THREAD_FOR_EACH
		{
			X86_CORE.fetch_current = (X86_CORE.fetch_current + 1) % x86_cpu_num_threads;
			if (x86_cpu_can_fetch(core, X86_CORE.fetch_current))
			{
				x86_cpu_fetch_thread(core, X86_CORE.fetch_current);
				break;
			}
		}
		break;
	}
	
	case x86_cpu_fetch_kind_switchonevent:
	{
		int must_switch;
		int new;

		/* If current thread is stalled, it means that we just switched to it.
		 * No fetching and no switching either. */
		thread = X86_CORE.fetch_current;
		if (X86_THREAD.fetch_stall_until >= x86_cpu->cycle)
			break;

		/* Switch thread if:
		 * - Quantum expired for current thread.
		 * - Long latency instruction is in progress. */
		must_switch = !x86_cpu_can_fetch(core, thread);
		must_switch = must_switch || x86_cpu->cycle - X86_CORE.fetch_switch_when >
			x86_cpu_thread_quantum + x86_cpu_thread_switch_penalty;
		must_switch = must_switch ||
			x86_event_queue_long_latency(core, thread);

		/* Switch thread */
		if (must_switch)
		{
			/* Find a new thread to switch to */
			for (new = (thread + 1) % x86_cpu_num_threads; new != thread;
				new = (new + 1) % x86_cpu_num_threads)
			{
				/* Do not choose it if it is not eligible for fetching */
				if (!x86_cpu_can_fetch(core, new))
					continue;
					
				/* Choose it if we need to switch */
				if (must_switch)
					break;

				/* Do not choose it if it is unfair */
				if (X86_THREAD_IDX(new).committed > X86_THREAD.committed + 100000)
					continue;

				/* Choose it if it is not stalled */
				if (!x86_event_queue_long_latency(core, new))
					break;
			}
				
			/* Thread switch successful? */
			if (new != thread)
			{
				X86_CORE.fetch_current = new;
				X86_CORE.fetch_switch_when = x86_cpu->cycle;
				X86_THREAD_IDX(new).fetch_stall_until = x86_cpu->cycle + x86_cpu_thread_switch_penalty - 1;
			}
		}

		/* Fetch */
		if (x86_cpu_can_fetch(core, X86_CORE.fetch_current))
			x86_cpu_fetch_thread(core, X86_CORE.fetch_current);
		break;
	}

	default:
		
		panic("%s: wrong fetch policy", __FUNCTION__);
	}
}


void x86_cpu_fetch()
{
	int core;

	x86_cpu->stage = "fetch";
	X86_CORE_FOR_EACH
		x86_cpu_fetch_core(core);
}