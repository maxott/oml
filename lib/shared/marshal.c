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
/** \file Implements marhsalling and unmarshalling of basic types for binary
 * transmission across the network.
 *
 * Marshalling is done directly into Mbuffers.
 *
 * \see marshal_init, marshal_measurements, marshal_values, marshal_finalize
 */

#include <math.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "oml2/omlc.h"
#include "ocomm/o_log.h"
#include "htonll.h"
#include "mem.h"
#include "mbuf.h"
#include "oml_value.h"
#include "marshal.h"

#define LENGTH(a) ((sizeof (a)) / (sizeof ((a)[0])))

#define BIG_S 15
#define BIG_L 30

#define LONG_T   0x1
#define DOUBLE_T 0x2
#define DOUBLE_NAN 0x3
#define STRING_T 0x4
#define INT32_T  0x5
#define UINT32_T 0x6
#define INT64_T  0x7
#define UINT64_T 0x8
#define BLOB_T   0x9

#define SYNC_BYTE 0xAA

/** Size of short marshalled message headers (OMB_DATA_P); OMB_LDATA_P are 2 bytes longer */
#define PACKET_HEADER_SIZE 5
#define STREAM_HEADER_SIZE 2

#define LONG_T_SIZE       4
#define DOUBLE_T_SIZE     5
/** Marshalled strings are limited to 254 characters */
#define STRING_T_MAX_SIZE 254
#define INT32_T_SIZE      4
#define UINT32_T_SIZE     4
#define INT64_T_SIZE      8
#define UINT64_T_SIZE     8
#define BLOB_T_MAX_SIZE   UINT32_MAX

#define MAX_STRING_LENGTH STRING_T_MAX_SIZE

#define MIN_LENGTH 64

/** Map from OML_*_VALUE types to protocol types. */
/* This array must be ordered identically to the OmlValueT enum. */
static const int oml_type_map [] =
  {
    DOUBLE_T,
    LONG_T,
    -1,
    STRING_T,
    INT32_T,
    UINT32_T,
    INT64_T,
    UINT64_T,
    BLOB_T
  };

/** Map from protocol types to OML_*_VALUE types.  */
/* This array must be ordered identically to the values of the protocol types. */
static const size_t protocol_type_map [] =
  {
    OML_UNKNOWN_VALUE,
    OML_LONG_VALUE,
    OML_DOUBLE_VALUE, // DOUBLE_T
    OML_DOUBLE_VALUE, // DOUBLE_NAN
    OML_STRING_VALUE,
    OML_INT32_VALUE,
    OML_UINT32_VALUE,
    OML_INT64_VALUE,
    OML_UINT64_VALUE,
    OML_BLOB_VALUE
  };

/** Map from protocol types to protocol sizes. */
/* NOTE: This array must be ordered identically to the values of the protocol types. */
static const size_t protocol_size_map [] =
  {
    -1,
    LONG_T_SIZE,
    DOUBLE_T_SIZE,
    DOUBLE_T_SIZE,
    STRING_T_MAX_SIZE,
    INT32_T_SIZE,
    UINT32_T_SIZE,
    INT64_T_SIZE,
    UINT64_T_SIZE,
    BLOB_T_MAX_SIZE
  };

/** Map from OML_*_VALUE types to size of protocol types on the wire. */
/* NOTE: This array must be ordered identically to the OmlValueT enum. */
static const size_t oml_size_map [] =
  {
    DOUBLE_T_SIZE,
    LONG_T_SIZE,
    -1,
    STRING_T_MAX_SIZE,
    INT32_T_SIZE,
    UINT32_T_SIZE,
    INT64_T_SIZE,
    UINT64_T_SIZE,
    BLOB_T_MAX_SIZE
  };

/** Find two synchronisation bytes (SYNC_BYTE) back to back.
 *
 * \param buf buffer to search for SYNC_BYTEs
 * \param len length of buf
 * \return a pointer to the first of two subsequent SYNC_BYTEs
 * */
unsigned char*
find_sync (unsigned char* buf, int len)
{
  int i;

  for (i = 1; i < len; i++)
      if (buf[i] == SYNC_BYTE && buf[i-1] == SYNC_BYTE)
        return &buf[i-1];

  return NULL;
}

/** Prepare a short marshalling header into an MBuffer.
 *
 * \param mbuf MBuffer to write the mbuf marshalling header to
 * \return the 0 on success, -1 on failure (\see mbuf_write)
 */
static int marshal_header_short (MBuffer *mbuf)
{
  uint8_t buf[] = {
    SYNC_BYTE, SYNC_BYTE, OMB_DATA_P, 0, 0
  };
  return mbuf_write (mbuf, buf, LENGTH (buf));
}

/** Prepare a long marshalling header into an MBuffer.
 *
 * \param mbuf MBuffer to write the mbuf marshalling header to
 * \return the 0 on success, -1 on failure (\see mbuf_write)
 */
static int marshal_header_long (MBuffer *mbuf)
{
  uint8_t buf[] = {
    SYNC_BYTE, SYNC_BYTE, OMB_LDATA_P, 0, 0, 0, 0
  };
  return mbuf_write (mbuf, buf, LENGTH (buf));
}

/** Retrieve the message type from an MBuffer containing a marshalling packet.
 *
 * \param mbuf MBuffer to read the mbuf marshalling header frow
 * \return the OmlBinMsgType of the packet
 */
OmlBinMsgType
marshal_get_msgtype (MBuffer *mbuf)
{
  return (OmlBinMsgType)(mbuf_message (mbuf))[2];
}

/** Initialise the MBuffer to serialise a new measurement packet, starting at
 * the current write pointer.
 *
 * Two basic types (OmlBinMsgType) of packets are available, short and long.
 * Short packets (OMB_DATA_P) can contain up to UINT16_MAX, whislt long packets
 * (OMB_LDATA_P) extend this to UINT32_MAX.
 *
 * Packets headers start with two SYNC_BYTEs (0xAA), then the packet type
 * (OMB_DATA_P or OMB_LDATA_P).
 * - OMB_DATA_P headers are 5 bytes long, the last two bytes containing the
 *   size of the message (including headers) as a 16-bit integer.
 * - OMB_LDATA_P headers are 7 bytes long, the last four bytes containing the
 *   size of the message (including headers) as a 32-bit integer.
 *
 * Metadata about the stream can then be packed in with marshal_measurements().
 *
 * This function can fail if there is a memory allocation failure or if the
 * buffer is misconfigured.
 *
 * \param mbuf MBuffer to serialize into
 * \param msgtype OmlBinMsgType of packet to build
 * \return 0 on success, -1 on failure
 * \see marshal_measurements
 */
int
marshal_init(MBuffer *mbuf, OmlBinMsgType msgtype)
{
  int result;
  if (mbuf == NULL) return -1;

  result = mbuf_begin_write (mbuf);
  if (result == -1) {
    logerror("Couldn't start marshalling packet (mbuf_begin_write())\n");
    return -1;
  }

  switch (msgtype) {
  case OMB_DATA_P:  result = marshal_header_short (mbuf); break;
  case OMB_LDATA_P: result = marshal_header_long (mbuf); break;
  }

  if (result == -1) {
    logerror("Error when trying to marshal packet header\n");
    return -1;
  }

  return 0;
}

/** Marshal meta-data for an OML measurement stream's sample
 *
 * An OML measurement stream is written as two bytes; the first one is the
 * counter for the number of elements in the message, and therefore starts at
 * 0, and the second one is the stream's index. This is followed by a
 * marshalled int32 value containing the sequence number, and a double value
 * containing the timestamp.
 *
 * A marshalling message should have been prepared in the MBuffer first with
 * marshal_init(). Actual data can then be marshalled into the message with
 * marshal_values().
 *
 *
 * \param mbuf MBuffer to write marshalled data to
 * \param stream Measurement Stream's index
 * \param seqno message sequence number
 * \param now message time
 * \return 1 if successful, -1 otherwise
 * \see marshal_init, marshal_values
 */
int
marshal_measurements(MBuffer* mbuf, int stream, int seqno, double now)
{
  OmlValueU v;
  uint8_t s[2] = { 0, (uint8_t)stream };
  int result = mbuf_write (mbuf, s, LENGTH (s));

  omlc_zero(v);

  if (result == -1) {
    logerror("Unable to marshal table number and measurement count (mbuf_write())\n");
    mbuf_reset_write (mbuf);
    return -1;
  }

  omlc_set_int32(v, seqno);
  marshal_value(mbuf, OML_INT32_VALUE, &v);

  omlc_set_double(v, now);
  marshal_value(mbuf, OML_DOUBLE_VALUE, &v);

  return 1;
}

/** Marshal the array of values into an MBuffer.
 *
 * Metadata of the measurement stream should already have been written with
 * marshal_measurements(). Each element of values is written with
 * marshal_value().  Finally, the number of elements in the message is updated
 * in its header, by incrementing the relevant field (depending on its
 * OmlBinMsgType) by value_count.
 *
 * If the returned number is negative, marshalling didn't finish as the
 * provided buffer was short of the number of bytes returned (when multiplied
 * by -1); the entire message has been reset (by marshal_value()), and
 * marshalling should restart with marshal_init(), after the MBuffer has been
 * adequately resized or repacked.
 *
 * Once all data has been marshalled, marshal_finalize() should be called to
 * finish preparing the message.
 *
 * \param mbuf MBuffer to write marshalled data to
 * \param values array of OmlValue of length value_count
 * \param value_count length  the values array
 * \return 1 on success, or -1 otherwise (marshalling should then restart from marshal_init())
 * \see marshal_init, marshal_measurements, marshal_value, marshal_finalize, mbuf_repack_message, mbuf_repack_message2, mbuf_resize
 */
int
marshal_values(MBuffer* mbuf, OmlValue* values, int value_count)
{
  OmlValue* val = values;
  int i, ret;

  for (i = 0; i < value_count; i++, val++) {
    if(!marshal_value(mbuf, oml_value_get_type(val), oml_value_get_value(val)))
      return -1;
  }

  uint8_t* buf = mbuf_message (mbuf);
  OmlBinMsgType type = marshal_get_msgtype (mbuf);
  switch (type) {
  case OMB_DATA_P: buf[5] += value_count; break;
  case OMB_LDATA_P: buf[7] += value_count; break;
  }
  return 1;
}

/** Marshal a single OmlValueU of type OmlValueT into mbuf.
 *
 * Usually called by marshal_values(). On failure, the whole message writing is
 * reset using mbuf_reset_write(), and marshalling should restart with
 * marshal_init(), after the MBuffer has been adequately resized or repacked.
 *
 * \param mbuf MBuffer to write marshalled data to
 * \param val_type OmlValueT representing the type of val
 * \param val pointer to OmlValueU, of type val_type, to marshall
 * \return 1 on success, or 0 otherwise (marshalling should then restart from marshal_init())
 * \see marshal_values, marshal_init, mbuf_reset_write, mbuf_repack_message, mbuf_repack_message2, mbuf_resize
 */
inline int
marshal_value(MBuffer* mbuf, OmlValueT val_type, OmlValueU* val)
{
  switch (val_type) {
  case OML_LONG_VALUE: {
    long v = oml_value_clamp_long (omlc_get_long(*val));
    uint32_t uv = (uint32_t)v;
    uint32_t nv = htonl(uv);
    uint8_t buf[LONG_T_SIZE+1];

    buf[0] = LONG_T;
    memcpy(&buf[1], &nv, sizeof (nv));

    logdebug("Marshalling long %ld\n", nv);
    int result = mbuf_write (mbuf, buf, LENGTH (buf));
    if (result == -1) {
      logerror("Failed to marshal OML_LONG_VALUE (mbuf_write())\n");
      mbuf_reset_write (mbuf);
      return 0;
    }
    break;
  }
  case OML_INT32_VALUE:
  case OML_UINT32_VALUE:
  case OML_INT64_VALUE:
  case OML_UINT64_VALUE: {
    uint8_t buf[UINT64_T_SIZE+1]; // Max integer size
    uint32_t uv32;
    uint32_t nv32;
    uint64_t uv64;
    uint64_t nv64;
    uint8_t *p_nv;

    if (oml_size_map[val_type] == 4)
      {
        uv32 = omlc_get_uint32(*val);
        nv32 = htonl(uv32);
        p_nv = (uint8_t*)&nv32;
      }
    else
      {
        uv64 = omlc_get_uint64(*val);
        nv64 = htonll(uv64);
        p_nv = (uint8_t*)&nv64;
      }

    logdebug("Marshalling %s\n", oml_type_to_s(val_type));
    buf[0] = oml_type_map[val_type];
    memcpy(&buf[1], p_nv, oml_size_map[val_type]);

    int result = mbuf_write (mbuf, buf, oml_size_map[val_type] + 1);
    if (result == -1)
      {
        logerror("Failed to marshal %s value (mbuf_write())\n",
               oml_type_to_s (val_type));
        mbuf_reset_write (mbuf);
        return 0;
      }
    break;
  }
  case OML_DOUBLE_VALUE: {
    uint8_t type = DOUBLE_T;
    double v = omlc_get_double(*val);
    int exp;
    double mant = frexp(v, &exp);
    int8_t nexp = (int8_t)exp;
    if (nexp != exp) {
      logerror("Double number '%lf' is out of bounds\n", v);
      type = DOUBLE_NAN;
      nexp = 0;
   }
   int32_t imant = (int32_t)(mant * (1 << BIG_L));
   uint32_t nmant = htonl(imant);

   uint8_t buf[6] = { type, 0, 0, 0, 0, nexp };

   memcpy(&buf[1], &nmant, sizeof (nmant));

   logdebug("Marshalling double %f\n", v);
   int result = mbuf_write (mbuf, buf, LENGTH (buf));

   if (result == -1)
     {
       logerror("Failed to marshal OML_DOUBLE_VALUE (mbuf_write())\n");
       mbuf_reset_write (mbuf);
       return 0;
     }
   break;
 }
 case OML_STRING_VALUE: {
   char* str = omlc_get_string_ptr(*val);

   if (str == NULL)
     {
       str = "";
       logwarn("Attempting to send a NULL string; sending empty string instead\n");
     }

   size_t len = strlen(str);
   if (len > STRING_T_MAX_SIZE) {
     logerror("Truncated string '%s'\n", str);
     len = STRING_T_MAX_SIZE;
   }

   uint8_t buf[2] = { STRING_T, (uint8_t)(len & 0xff) };
   int result = mbuf_write (mbuf, buf, LENGTH (buf));

   if (result == -1)
     {
       logerror("Failed to marshal OML_STRING_VALUE type and length (mbuf_write())\n");
       mbuf_reset_write (mbuf);
       return 0;
     }

   result = mbuf_write (mbuf, (uint8_t*)str, len);

   if (result == -1)
     {
       logerror("Failed to marshal OML_STRING_VALUE (mbuf_write())\n");
       mbuf_reset_write (mbuf);
       return 0;
     }
   break;
 }
 case OML_BLOB_VALUE: {
   int result = 0;
   void *blob = omlc_get_blob_ptr(*val);
   size_t length = omlc_get_blob_length(*val);
   if (blob == NULL || length == 0) {
     logwarn ("Attempting to send NULL or empty blob; blob of length 0 will be sent\n");
     length = 0;
   }

   uint8_t buf[5] = { BLOB_T, 0, 0, 0, 0 };
   size_t n_length = htonl (length);
   memcpy (&buf[1], &n_length, 4);

   logdebug("Marshalling blob of size %d.\n", length);
   result = mbuf_write (mbuf, buf, sizeof (buf));

   if (result == -1) {
     logerror ("Failed to marshall OML_BLOB_VALUE type and length (mbuf_write())\n");
     mbuf_reset_write (mbuf);
     return 0;
   }

   result = mbuf_write (mbuf, blob, length);

   if (result == -1) {
     logerror ("Failed to marshall %d bytes of OML_BLOB_VALUE data\n", length);
     mbuf_reset_write (mbuf);
     return 0;
   }
   break;
 }
 default:
   logerror("Unsupported value type '%d'\n", val_type);
   return 0;
 }

  return 1;
}

/** Finalise a marshalled message.
 *
 * Depending on the number of values packed, change the type of message, and
 * write the actual size in the right location of the header, in network byte
 * order.
 *
 * \param mbuf MBuffer where marshalled data is
 * \return 1
 * \see marshal_init, marshal_measurements, marshal_values
 */
int
marshal_finalize(MBuffer*  mbuf)
{
  uint8_t* buf = mbuf_message (mbuf);
  OmlBinMsgType type = marshal_get_msgtype (mbuf);
  size_t len = mbuf_message_length (mbuf);

  if (len > UINT32_MAX) {
    logwarn("Message length %d longer than maximum packet length (%d); "
            "packet will be truncated\n",
            len, UINT32_MAX);
    len = UINT32_MAX;
  }


  if (type == OMB_DATA_P && len > UINT16_MAX) {
    /*
     * We assumed a short packet, but there is too much data, so we
     * have to shift the whole buffer down by 2 bytes and convert to a
     * long packet.
     */
    uint8_t s[2] = {0};
    /* Put some padding in the buffer to make sure it has room, and maintains its invariants */
    mbuf_write (mbuf, s, sizeof (s));
    memmove (&buf[PACKET_HEADER_SIZE+2], &buf[PACKET_HEADER_SIZE],
             len - PACKET_HEADER_SIZE);
    len += 2;
    buf[2] = type = OMB_LDATA_P;
  }


  switch (type) {
  case OMB_DATA_P:
    len -= PACKET_HEADER_SIZE; // Data length minus header
    uint16_t nlen16 = htons (len);
    memcpy (&buf[3], &nlen16, sizeof (nlen16));
    break;
  case OMB_LDATA_P:
    len -= PACKET_HEADER_SIZE + 2; // Data length minus header
    uint32_t nlen32 = htonl (len); // pure data length
    memcpy (&buf[3], &nlen32, sizeof (nlen32));
    break;
  }

  return 1;
}

/** Read the marshalling header information contained in an MBuffer.
 *
 * \param mbuf MBuffer to read from
 * \param header pointer to an OmlBinaryHeader into which the data from the
 *               mbuf should be unmarshalled
 * \return 1 on success, the size of the missing section as a negative number
 *         if the buffer is too short, or 0 if something failed
 */
int
unmarshal_init(MBuffer* mbuf, OmlBinaryHeader* header)
{
  uint8_t header_str[PACKET_HEADER_SIZE + 2];
  uint8_t stream_header_str[STREAM_HEADER_SIZE];
  int result;
  OmlValue seqno;
  OmlValue timestamp;

  oml_value_init(&seqno);
  oml_value_init(&timestamp);

  result = mbuf_begin_read (mbuf);
  if (result == -1) {
    logerror("Couldn't start unmarshalling packet (mbuf_begin_read())\n");
    return 0;
  }

  result = mbuf_read (mbuf, header_str, 3);
  if (result == -1) {
    return mbuf_remaining (mbuf) - 3;
  }

  if (! (header_str[0] == SYNC_BYTE && header_str[1] == SYNC_BYTE)) {
    logdebug("Cannot find sync bytes in binary stream, out of sync; first 3 bytes: %#0x %#0x %#0x\n",
        header_str[0], header_str[1], header_str[2]);
    return 0;
  }

  header->type = (OmlBinMsgType)header_str[2];

  if (header->type == OMB_DATA_P) {
    // Read 2 more bytes of the length field
    uint16_t nv16 = 0;
    result = mbuf_read (mbuf, (uint8_t*)&nv16, sizeof (uint16_t));
    if (result == -1) {
      int n = mbuf_remaining (mbuf) - 2;
      mbuf_reset_read (mbuf);
      return n;
    }
    header->length = (int)ntohs (nv16);
  } else if (header->type == OMB_LDATA_P) {
    // Read 4 more bytes of the length field
    uint32_t nv32 = 0;
    result = mbuf_read (mbuf, (uint8_t*)&nv32, sizeof (uint32_t));
    if (result == -1) {
      int n = mbuf_remaining (mbuf) - 4;
      mbuf_reset_read (mbuf);
      return n;
    }
    header->length = (int)ntohl (nv32);
  } else {
    logwarn ("Unknown packet type %d\n", (int)header->type);
    return 0;
  }

  int extra = mbuf_remaining (mbuf) - header->length;
  if (extra < 0) {
    mbuf_reset_read (mbuf);
    return extra;
  }

  result = mbuf_read (mbuf, stream_header_str, LENGTH (stream_header_str));
  if (result == -1)
    {
      logerror("Unable to read stream header\n");
      return 0;
    }

  header->values = (int)stream_header_str[0];
  header->stream = (int)stream_header_str[1];

  if (unmarshal_typed_value (mbuf, "seq-no", OML_INT32_VALUE, &seqno) == -1)
    return 0;

  if (unmarshal_typed_value (mbuf, "timestamp", OML_DOUBLE_VALUE, &timestamp) == -1)
    return 0;

  header->seqno = omlc_get_int32(*oml_value_get_value(&seqno));
  header->timestamp = omlc_get_double(*oml_value_get_value(&timestamp));

  oml_value_reset(&seqno);
  oml_value_reset(&timestamp);

  return 1;
}

/** \see unmarshal_values
 */
inline int
unmarshal_measurements(
  MBuffer* mbuf,
  OmlBinaryHeader* header,
  OmlValue*   values,
  int         max_value_count
) {
  return unmarshal_values(mbuf, header, values, max_value_count);
}

/** Unmarshals the content of buffer into an array of values of size
 * value_count.
 *
 * If the returned number is negative, there were more values than could fit in
 * the array in the buffer, and some were skipped.  This number (when
 * multiplied by -1) indicates  by how much the values array should be
 * extended. If the number is less than
 * -100, it indicates an error.
 *
 * \param mbuf MBuffer to read from
 * \param header pointer to an OmlBinaryHeader corresponding to this message
 * \param values array of OmlValue to be filled
 * \param max_value_count length of the array (XXX: Should be < 100, otherwise confusion may happen with error returns)
 * \return the number of values found (positive), or the number of values that didn't fit in the array (negative; multiplied by -1), <-100 in case of error
 * \see unmarshal_init
 */
int
unmarshal_values(
  MBuffer*  mbuf,
  OmlBinaryHeader* header,
  OmlValue*    values,
  int          max_value_count
) {
  int value_count = header->values;

  if (0 == value_count) {
    logwarn("No value to unmarshall\n");
    return -102;
  }

  if (value_count > max_value_count) {
    logwarn("Measurement packet contained %d too many values for internal storage (max %d, actual %d); skipping packet\n",
           (value_count - max_value_count), max_value_count, value_count);
    logwarn("Message length appears to be %d + 5\n", header->length);

    mbuf_read_skip (mbuf, header->length + PACKET_HEADER_SIZE);
    mbuf_begin_read (mbuf);

    // FIXME:  Check for sync
    return max_value_count - value_count;  // value array is too small
  }

  int i;
  OmlValue* val = values;
  for (i = 0; i < value_count; i++, val++) {
    if (unmarshal_value(mbuf, val) == 0) {
      logwarn("Could not unmarshal values %d of %d\n", i, value_count);
      return -101;
    }
  }
  return value_count;
}

/** Unmarshals the next content of an MBuffer into a OmlValue
 *
 * \param mbuf MBuffer to read from
 * \param value pointer to OmlValue to unmarshall the read data into
 * \return 1 if successful, 0 otherwise
 */
int
unmarshal_value(
  MBuffer*  mbuf,
  OmlValue*    value
) {
  if (mbuf_remaining(mbuf) == 0) {
      o_log(O_LOG_ERROR, "Tried to unmarshal a value from the buffer, but didn't receive enough data to do that\n");
      return 0;
  }

  int type = mbuf_read_byte (mbuf);
  if (type == -1) return 0;
  OmlValueT oml_type = protocol_type_map[type];

  switch (type) {
  case LONG_T: {
    uint8_t buf [LONG_T_SIZE];

    if (mbuf_read (mbuf, buf, LENGTH (buf)) == -1)
      {
        logerror("Failed to unmarshal OML_LONG_VALUE; not enough data?\n");
        return 0;
      }

    uint32_t hv = ntohl(*((uint32_t*)buf));
    int32_t v = (int32_t)(hv);

    /*
     * The server no longer needs to know about OML_LONG_VALUE, as the
     * marshalling process now maps OML_LONG_VALUE into OML_INT32_VALUE
     * (by truncating to [INT_MIN, INT_MAX].  Therefore, unmarshall a
     * LONG_T value into an OML_INT32_VALUE object.
     */
    oml_value_set_type(value, OML_INT32_VALUE);
    value->value.int32Value = v;
    break;
  }
  case INT32_T:
  case UINT32_T:
  case INT64_T:
  case UINT64_T: {
    uint8_t buf [UINT64_T_SIZE]; // Maximum integer size

    if (mbuf_read (mbuf, buf, protocol_size_map[type]) == -1)
      {
        logerror("Failed to unmarshall %d value; not enough data?\n",
               type);
        return 0;
      }

    oml_value_set_type(value, oml_type);
    if (protocol_size_map[type] == 4)
      value->value.uint32Value = ntohl(*((uint32_t*)buf));
    else
      value->value.uint64Value = ntohll(*((uint64_t*)buf));
    break;
  }
    case DOUBLE_T: {
      uint8_t buf [DOUBLE_T_SIZE];

      if (mbuf_read (mbuf, buf, LENGTH (buf)) == -1)
        {
          logerror("Failed to unmarshal OML_DOUBLE_VALUE; not enough data?\n");
          return 0;
        }

      int hmant = (int)ntohl(*((uint32_t*)buf));
      double mant = hmant * 1.0 / (1 << BIG_L);
      int exp = (int8_t) buf[4];
      double v = ldexp(mant, exp);
      oml_value_set_type(value, oml_type);
      value->value.doubleValue = v;
      break;
    }
    case STRING_T: {
      int len = 0;
      uint8_t buf [STRING_T_MAX_SIZE];

      len = mbuf_read_byte (mbuf);

      if (len == -1 || mbuf_read (mbuf, buf, len) == -1)
        {
          logerror("Failed to unmarshal OML_STRING_VALUE; not enough data?\n");
          return 0;
        }

      oml_value_set_type(value, OML_STRING_VALUE);
      omlc_set_string_copy(*oml_value_get_value(value), buf, len);
      break;
    }
    case BLOB_T: {
      uint32_t n_len;

      if (mbuf_read (mbuf, (uint8_t*)&n_len, 4) == -1) {
        logerror ("Failed to unmarshal OML_BLOB_VALUE length field; not enough data?\n");
        return 0;
      }

      size_t len = ntohl (n_len);
      size_t remaining = mbuf_remaining (mbuf);

      if (len > remaining) {
        logerror ("Failed to unmarshal OML_BLOB_VALUE data:  not enough data available "
                  "(wanted %d, but only have %d bytes\n",
                  len, remaining);
        return 0;
      }

      void *ptr = mbuf_rdptr (mbuf);
      oml_value_set_type(value, OML_BLOB_VALUE);
      omlc_set_blob (*oml_value_get_value(value), ptr, len); /*XXX*/
      mbuf_read_skip (mbuf, len);
      break;
    }
    default:
      logerror("Unsupported value type '%d'\n", type);
      return 0;
  }

  return 1;
}

/** Unmarshals the next content of an MBuffer into an OmlValue with
 * type-checking.
 *
 * \param mbuf MBuffer to read from
 * \param type OmlValueT specifying the type to unmarshall
 * \param value pointer to OmlValue to unmarshall the read data into
 * \return 1 if successful, 0 otherwise (e.g., type mismatch)
 */
int
unmarshal_typed_value (MBuffer* mbuf, const char* name, OmlValueT type, OmlValue* value)
{
  if (unmarshal_value (mbuf, value) != 1) {
      logerror("Error reading %s from binary packet\n", name);
      return -1;
  }

  if (oml_value_get_type(value) != type) {
      logerror("Expected type '%s' for %s, but got type '%s' instead\n",
             oml_type_to_s (type), name, oml_type_to_s (oml_value_get_type(value)));
      return -1;
  }
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
