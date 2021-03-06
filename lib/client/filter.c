/*
 * Copyright 2007-2013 National ICT Australia (NICTA), Australia
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
/*!\file filter.c
  \brief Implements OML's client side filter engine
*/

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#define _XOPEN_SOURCE 500     /* Or: #define _BSD_SOURCE; for useconds_t to be defined */
#include <unistd.h>

#include "oml2/omlc.h"
#include "oml2/oml_filter.h"
#include "oml2/oml_writer.h"
#include "ocomm/o_log.h"
#include "client.h"

static void* thread_start(void* handle);

extern OmlClient* omlc_instance;
/**
 * \fn void filter_engine_start( OmlMStream* ms )
 * \brief function that will start the filter thread
 * \param ms the stream to filter
 */
void
filter_engine_start(
  OmlMStream* ms
) {
  pthread_create(&ms->filter_thread, NULL, thread_start, (void*)ms);
}
/**
 * \fn static void* thread_start(void* handle)
 * \brief start the filter thread
 * \param handle the stream to use the filters on
 */
static void*
thread_start(
  void* handle
) {
  OmlMStream* ms = (OmlMStream*)handle;
  OmlMP* mp = ms->mp;
  useconds_t usec = (useconds_t)(1000000 * ms->sample_interval);
  int status = 0;

  while (1)
    {
      usleep(usec);
      if (!mp_lock(mp))
        {
          if (!mp->active)
            {
              mp_unlock(mp);
              return NULL;  // we are done
            }
          status = filter_process(ms);
          mp_unlock(mp);
        }
      if (status == -1)
        return NULL; // Fatal error --> exit thread
    }
}


/** Run filters associated to an MS.
 *
 * Get the writer associated to the MS, and generate and write initial metadata
 * (seqno and time). Then, instruct all the filters, in sequence, to write
 * there filtered sample to this writer before finalising the write.
 *
 * \param ms MS to generate output for
 * \return 0 if success, -1 otherwise
 *
 * \see OmlWriter, oml_writer_row_start, oml_writer_out, oml_writer_row_end
 */
int filter_process(OmlMStream* ms)
{
  if (ms == NULL || omlc_instance == NULL)
    {
      logerror("Could not process filters because of null measurement stream or instance\n");
      return -1;
    }

  OmlWriter* writer = ms->writer;

  if (writer == NULL)
    {
      logerror("Could not process filters because of null writer\n");
      return -1;
    }

  struct timeval tv;
  gettimeofday(&tv, NULL);
  double now = tv.tv_sec - omlc_instance->start_time + 0.000001 * tv.tv_usec;

  // Be aware that +row_start+ is obtaining a lock on the writer
  // which is released in +row_end+. Always ensure that +row_end+ is
  // called, even if there is a problem somewhere along the way.
  //
  ms->seq_no++;
  writer->row_start(writer, ms, now);
  OmlFilter* f = ms->firstFilter;
  for (; f != NULL; f = f->next)
    f->output(f, writer);
  writer->row_end(writer, ms);
  ms->sample_size = 0;
  //  oml_unlock(&writer->lock, "filter");

  return 0;
}

/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
