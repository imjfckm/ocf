/*
 * Copyright(c) 2012-2018 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "metadata.h"
#include "metadata_io.h"
#include "metadata_updater_priv.h"
#include "../ocf_priv.h"
#include "../engine/engine_common.h"
#include "../ocf_cache_priv.h"
#include "../ocf_ctx_priv.h"
#include "../utils/utils_io.h"
#include "../utils/utils_realloc.h"

int ocf_metadata_updater_init(ocf_cache_t cache)
{
	ocf_metadata_updater_t mu = &cache->metadata_updater;
	struct ocf_metadata_io_syncher *syncher = &mu->syncher;

	INIT_LIST_HEAD(&syncher->in_progress_head);
	INIT_LIST_HEAD(&syncher->pending_head);
	env_mutex_init(&syncher->lock);

	return ctx_metadata_updater_init(cache->owner, mu);
}

void ocf_metadata_updater_kick(ocf_cache_t cache)
{
	ctx_metadata_updater_kick(cache->owner, &cache->metadata_updater);
}

void ocf_metadata_updater_stop(ocf_cache_t cache)
{
	ctx_metadata_updater_stop(cache->owner, &cache->metadata_updater);
}

void ocf_metadata_updater_set_priv(ocf_metadata_updater_t mu, void *priv)
{
	OCF_CHECK_NULL(mu);
	mu->priv = priv;
}

void *ocf_metadata_updater_get_priv(ocf_metadata_updater_t mu)
{
	OCF_CHECK_NULL(mu);
	return mu->priv;
}

ocf_cache_t ocf_metadata_updater_get_cache(ocf_metadata_updater_t mu)
{
	OCF_CHECK_NULL(mu);
	return container_of(mu, struct ocf_cache, metadata_updater);
}

static int _metadata_updater_iterate_in_progress(ocf_cache_t cache,
		struct metadata_io_request *new_req)
{
	struct metadata_io_request_asynch *a_req;
	struct ocf_metadata_io_syncher *syncher =
		&cache->metadata_updater.syncher;
	struct metadata_io_request *curr, *temp;

	list_for_each_entry_safe(curr, temp, &syncher->in_progress_head, list) {
		if (env_atomic_read(&curr->finished)) {
			a_req = curr->asynch;
			ENV_BUG_ON(!a_req);

			list_del(&curr->list);

			if (env_atomic_dec_return(&a_req->req_active) == 0) {
				OCF_REALLOC_DEINIT(&a_req->reqs,
						&a_req->reqs_limit);
				env_free(a_req);
			}
			continue;
		}
		if (new_req) {
			/* If request specified, check if overlap occurs. */
			if (ocf_io_overlaps(new_req->page, new_req->count,
					curr->page, curr->count)) {
				return 1;
			}
		}
	}

	return 0;
}

int metadata_updater_check_overlaps(ocf_cache_t cache,
                struct metadata_io_request *req)
{
	struct ocf_metadata_io_syncher *syncher =
		&cache->metadata_updater.syncher;
	int ret;

	env_mutex_lock(&syncher->lock);

	ret = _metadata_updater_iterate_in_progress(cache, req);

	/* Either add it to in-progress list or pending list for deferred
	 * execution.
	 */
	if (ret == 0)
		list_add_tail(&req->list, &syncher->in_progress_head);
	else
		list_add_tail(&req->list, &syncher->pending_head);

	env_mutex_unlock(&syncher->lock);

	return ret;
}

uint32_t ocf_metadata_updater_run(ocf_metadata_updater_t mu)
{
	struct metadata_io_request *curr, *temp;
	struct ocf_metadata_io_syncher *syncher;
	ocf_cache_t cache;
	int ret;

	OCF_CHECK_NULL(mu);

	cache = ocf_metadata_updater_get_cache(mu);
	syncher = &cache->metadata_updater.syncher;

	env_mutex_lock(&syncher->lock);
	if (list_empty(&syncher->pending_head)) {
		/*
		 * If pending list is empty, we iterate over in progress
		 * list to free memory used by finished requests.
		 */
		_metadata_updater_iterate_in_progress(cache, NULL);
		env_mutex_unlock(&syncher->lock);
		env_cond_resched();
		return 0;
	}
	list_for_each_entry_safe(curr, temp, &syncher->pending_head, list) {
		ret = _metadata_updater_iterate_in_progress(cache, curr);
		if (ret == 0) {
			/* Move to in-progress list and kick the workers */
			list_move_tail(&curr->list, &syncher->in_progress_head);
		}
		env_mutex_unlock(&syncher->lock);
		if (ret == 0)
			ocf_engine_push_req_front(&curr->fl_req, true);
		env_cond_resched();
		env_mutex_lock(&syncher->lock);
	}
	env_mutex_unlock(&syncher->lock);

	return 0;
}
