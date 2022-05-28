//
// Created by switchblade on 28/05/22.
//

#include "zstd_util.hpp"

namespace sek::detail
{
	void zstd_thread_ctx::zstd_dstream::decompress_frame(buffer_t &comp_buff, buffer_t &decomp_buff)
	{
		ZSTD_inBuffer in_buff = comp_buff;
		ZSTD_outBuffer out_buff = decomp_buff;

		for (;;)
		{
			const auto res = assert_zstd_error(ZSTD_decompressStream(*this, &out_buff, &in_buff));
			if (res == 0) [[likely]]
				break;
			else if (out_buff.pos < out_buff.size) [[unlikely]] /* Incomplete input frame. */
			{
				comp_buff.reset();
				decomp_buff.reset();
				throw zstd_error("Incomplete or invalid ZSTD frame");
			}

			/* Not enough space in the output buffer, allocate more. */
			decomp_buff.expand(decomp_buff.size + res);
			out_buff.dst = decomp_buff.data;
			out_buff.size = decomp_buff.size;
		}

		/* Need to reset the stream after decompression, since frames are compressed independently. */
		reset();
	}
	bool zstd_thread_ctx::fill_decomp_frame(buffer_t &comp_buff, buffer_t &decomp_buff)
	{
		frame_header header;
		/* Failed to read frame header. Even if it is not EOF yet, there is no usable frames for
		 * decompression there, so treat it as the end of compressed data. */
		if (!read_frame_header(header)) [[unlikely]]
			return false;

		/* Allocate input & output buffers. */
		if (comp_buff.expand(header.comp_size)) [[unlikely]]
			throw std::bad_alloc();
		else if (decomp_buff.expand(header.src_size)) [[unlikely]]
		{
			comp_buff.reset();
			throw std::bad_alloc();
		}
		else if (!read_checked(comp_buff.data, comp_buff.size)) [[unlikely]] /* Handle premature EOF. */
		{
			comp_buff.reset();
			decomp_buff.reset();
			return false;
		}
		return true;
	}
	void zstd_thread_ctx::decompress_threaded()
	{
		auto &stream = zstd_dstream::instance();
		stream.reset();

		buffer_t comp_buff; /* Compressed buffer is re-used between frames. */
		for (;;)
		{
			thread_task task;

			/* Obtain a task & compressed data from the context. */
			{
				auto l = guard_read();
				task = reuse_task_buffer(); /* Attempt to re-use a previously committed buffer. */

				/* Failure to fill next frame means we are at the end of compressed data. */
				if (!fill_decomp_frame(comp_buff, task)) [[unlikely]]
					break;
				task.frame_idx = in_frame++;
			}

			/* At this point we have a valid task & a filled compressed data buffer. */
			stream.decompress_frame(comp_buff, task);

			/* All data of the frame has been flushed, can submit the task now. */
			{
				const auto l = guard_write();
				if (!submit(task)) [[unlikely]]
				{
					comp_buff.reset();
					throw zstd_error("Failed to submit decompression task");
				}
			}
		}
		comp_buff.reset();
	}
	void zstd_thread_ctx::decompress_single()
	{
		auto &stream = zstd_dstream::instance();
		stream.reset();

		buffer_t comp_buff;
		buffer_t decomp_buff;
		for (;;)
		{
			/* Read next frame into the compressed buffer and initialize the decompressed buffer.
			 * Failure to fill next frame means we are at the end of compressed data. */
			if (!fill_decomp_frame(comp_buff, decomp_buff)) [[unlikely]]
				break;

			/* Decompress & directly write the decompressed data to the output. */
			stream.decompress_frame(comp_buff, decomp_buff);
			if (!write_checked(decomp_buff.data, decomp_buff.size)) [[unlikely]]
			{
				comp_buff.reset();
				decomp_buff.reset();
				throw zstd_error("Failed to write decompression result");
			}
		}
		comp_buff.reset();
		decomp_buff.reset();
	}
	void zstd_thread_ctx::decompress(thread_pool &pool, zstd_thread_ctx::read_t r, zstd_thread_ctx::write_t w)
	{
		init(r, w);

		const auto tasks = math::min(pool.size(), max_tasks);
		if (tasks == 1) [[unlikely]] /* If there is only 1 task available, do single-threaded decompression. */
			decompress_single();
		else
		{
#ifdef ALLOCA
			/* Stack allocation here is fine, since std::future is not a large structure. */
			auto *fut_buf = static_cast<std::future<void> *>(ALLOCA(sizeof(std::future<void>) * tasks));
#else
			auto *fut_buf = static_cast<std::future<void> *>(::operator new(sizeof(std::future<void>) * total_threads));
#endif

			std::exception_ptr eptr;
			try
			{
				/* Schedule pool.size() tasks. Some of these tasks will terminate without doing anything.
				 * This is not ideal, but we can not know the amount of frames beforehand. */
				for (std::size_t i = 0; i < tasks; ++i)
					std::construct_at(fut_buf + i, pool.schedule([this]() { decompress_threaded(); }));

				/* Wait for threads to terminate. Any exceptions will be re-thrown by the worker's future. */
				for (std::size_t i = 0; i < tasks; ++i) fut_buf[i].get();
			}
			catch (std::bad_alloc &)
			{
				throw;
			}
			catch (...)
			{
				/* Store thrown exception, since we still need to clean up allocated buffers. */
				eptr = std::current_exception();
			}

			clear_tasks();
			for (std::size_t i = 0; i < tasks; ++i) std::destroy_at(fut_buf + i);
#ifndef ALLOCA
			::operator delete(fut_buf);
#endif

			if (eptr) [[unlikely]]
				std::rethrow_exception(eptr);
		}
	}
}	 // namespace sek::detail