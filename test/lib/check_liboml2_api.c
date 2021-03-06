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
#include <string.h>
#include <check.h>

#include "oml2/omlc.h"
#include "validate.h"
#include "client.h"

#define LENGTH(a) (sizeof(a)/sizeof(a[0]))

typedef struct
{
  const char* name;
  int is_valid_name;
  int is_valid_app_name;
} Name;


static Name names_vector [] =
  {
    { "internal space",         0, 0 },
    { "internal space two",     0, 0 },
    { "internal space three x", 0, 0 },
    { " leadingspace",          0, 0 },
    { "  leadingspace",         0, 0 },
    { "   leadingspace",        0, 0 },
    { "trailingspace ",         0, 0 },
    { "trailingspace  ",        0, 0 },
    { "trailingspace   ",       0, 0 },
    { " leading space",         0, 0 },
    { "  leading space",        0, 0 },
    { "   leading space",       0, 0 },
    { "trailing space ",        0, 0 },
    { "trailing space  ",       0, 0 },
    { "trailing space   ",      0, 0 },
    { " leadingspaceandtrailingspace ",       0, 0},
    { "  leadingspaceandtrailingspace  ",     0, 0},
    { "   leadingspaceandtrailingspace   ",   0, 0},
    { "    leadingspaceandtrailingspace    ", 0, 0},
    { " leading and internal space",    0, 0 },
    { "  leading and internal space",   0, 0 },
    { "   leading and internal space",  0, 0 },
    { "internal and trailing space ",   0, 0 },
    { "internal and trailing space  ",  0, 0 },
    { "internal and trailing space   ", 0, 0 },
    { "",                       0, 0 },
    { " ",                      0, 0 },
    { "   ",                    0, 0 },
    { "     ",                  0, 0 },
    { "validname",              1, 1 },
    { "valid_name",             1, 1 },
    { "valid/name",             0, 1 },
    { "valid/app/name",         0, 1 },
    { "/",                      0, 0 },
    { "v",                      1, 1 },
    { "_",                      1, 1 },
    { "1",                      0, 0 },
    { "1_invalid_name",         0, 0 },
    { "1invalidname",           0, 0 },
    { "validname1",             1, 1 },
    { "valid2name",             1, 1 },
    { "valid_234_name",         1, 1 },
    { "1/valid/app/name",       0, 1 },
    { "1/invalid/app/name/",    0, 0 },
  };

/******************************************************************************/
/*                    APP and MP NAME HANDLING CHECKS                         */
/******************************************************************************/

START_TEST (test_api_app_name_spaces)
{
  int res;
  res = omlc_init (names_vector[_i].name, 0, NULL, NULL);
  if (names_vector[_i].is_valid_app_name)
    {
      fail_if (res != 0, "Valid app name '%s' was rejected\n", names_vector[_i]);
      fail_if (omlc_instance == NULL);
    }
  else
    {
      fail_if (res != -1, "Invalid app name '%s' was incorrectly accepted\n", names_vector[_i]);
      fail_unless (omlc_instance == NULL);
    }
}
END_TEST

START_TEST (test_api_validate_mp_name)
{
  int res = validate_name (names_vector[_i].name);
  if (names_vector[_i].is_valid_name) {
    fail_if (res == 0, "MP name '%s' incorrectly marked as invalid\n", names_vector[_i].name);
  } else {
    fail_unless (res == 0, "MP name '%s' incorrectly marked as valid\n", names_vector[_i].name);
  }
}
END_TEST

START_TEST (test_api_mp_name_spaces)
{
  OmlMPDef def [] =
    {
      { "field1", OML_INT32_VALUE },
      { NULL, (OmlValueT)0 }
    };

  OmlClient dummy;
  omlc_instance = &dummy;

  const char* name = names_vector[_i].name;
  int is_valid = names_vector[_i].is_valid_name;

  OmlMP* res = omlc_add_mp (name, def);

  if (is_valid)
    fail_if (res == NULL, "omlc_add_mp () failed to create MP for valid name '%s'\n", name);
  else
    fail_unless (res == NULL, "omlc_add_mp() created an MP for invalid name '%s'\n", name);
}
END_TEST

Suite*
api_suite (void)
{
  Suite* s = suite_create("API");

  /* API test cases */
  TCase* tc_api_names = tcase_create("ApiNames");

  /* Add tests to test case "ApiNames" */
  tcase_add_loop_test (tc_api_names, test_api_app_name_spaces, 0, LENGTH(names_vector));
  tcase_add_loop_test (tc_api_names, test_api_validate_mp_name, 0, LENGTH(names_vector));
  tcase_add_loop_test (tc_api_names, test_api_mp_name_spaces, 0, LENGTH(names_vector));

  suite_add_tcase (s, tc_api_names);

  return s;
}



/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
