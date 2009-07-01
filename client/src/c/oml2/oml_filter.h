/*
 * Copyright 2007-2009 National ICT Australia (NICTA), Australia
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
#ifndef OML_FILTER_H_
#define OML_FILTER_H_

#include <oml2/omlc.h>
#include <oml2/oml_writer.h>

struct _omlFilter;

typedef struct _omlFilter* (*oml_filter_create)(
  const char* paramName,
  OmlValueT   type,
  int         index
);


/*! Function to set filter parameters.
 *
 * Return 0 on success, -1 otherwise
 */
typedef int (*oml_filter_set)(
  struct _omlFilter* filter, //! pointer to filter instance
  const char* name,  //! Name of parameter
  OmlValue* value    //! Value of paramter
);


/*! Called for every sample.
 *
 * Return 0 on success, -1 otherwise
 */
typedef int (*oml_filter_input)(
  struct _omlFilter* filter, //! pointer to filter instance
  OmlValueU*         values,  //! values of sample
  OmlMP*             mp      //! MP context
);


/*! Called to request the current filter output. This is most likely
 * some function over the samples received since the last call.
 *
 * Return 0 on success, -1 otherwise
 */
typedef int (*oml_filter_output)(
  struct _omlFilter* filter,
  struct _omlWriter* writer //! Write results of filter to this function
);

/*! Called once to write out the filters meta data
 * on its result arguments.
 */
typedef int (*oml_filter_meta)(
  struct _omlFilter* filter,
  int index_offset,  //! Index to use for first parameter
  char** namePtr,
  OmlValueT* type
);

/**
 * \struct
 * \brief
 */
typedef struct _omlFilter {
  //! Prefix for column name
  char name[64];

  //! Number of output value created
  int output_cnt;

  //! Set filter parameters
  oml_filter_set set;

  //! Process a new sample.
  oml_filter_input input;

  //! Calculate output, send it, and get ready for new sample period.
  oml_filter_output output;

  oml_filter_meta meta;

  struct _omlFilter* next;
} OmlFilter;

/*! Create an instance of a filter of type 'fname'.
 */
void
register_filter(
    const char*       filterName,
    oml_filter_create create_func
);

/*! Create an instance of a filter of type 'fname'.
 */
OmlFilter*
create_filter(
    const char* filterName,
    const char* paramName,
    OmlValueT   type,
    int         index
);
#endif /*OML_FILTER_H_*/

/*
 Local Variables:
 mode: C
 tab-width: 4
 indent-tabs-mode: nil
*/
