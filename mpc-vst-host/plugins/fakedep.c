/* SPDX-License-Identifier: MIT
 * libmpcvst_fakedep: linked by the BAD_MISSING_DEP plug-in, then deleted
 * so the plug-in's dependency is missing at scan time. */
__attribute__((visibility("default"))) int mock_fakedep_value(void) { return 7; }
