/*
 * Copyright 2013-2016 Guardtime, Inc.
 *
 * This file is part of the Guardtime client SDK.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES, CONDITIONS, OR OTHER LICENSES OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 * "Guardtime" and "KSI" are trademarks or registered trademarks of
 * Guardtime, Inc., and no license to trademarks is granted; Guardtime
 * reserves and retains all trademark rights.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ksi/ksi.h>
#include <ksi/compatibility.h>
#include "param_set/param_set.h"
#include "param_set/task_def.h"
#include "api_wrapper.h"
#include "tool_box/param_control.h"
#include "tool_box/ksi_init.h"
#include "tool_box/task_initializer.h"
#include "debug_print.h"
#include "smart_file.h"
#include "err_trckr.h"
#include "printer.h"
#include "obj_printer.h"
#include "conf_file.h"
#include "tool.h"
#include "param_set/parameter.h"

static int generate_tasks_set(PARAM_SET *set, TASK_SET *task_set);
static int sign_save_to_file(PARAM_SET *set, KSI_CTX *ksi, ERR_TRCKR *err, KSI_Signature **sig);
static char* get_output_file_name_if_not_defined(PARAM_SET *set, ERR_TRCKR *err, char *buf, size_t buf_len);
static int check_pipe_errors(PARAM_SET *set, ERR_TRCKR *err);
static int check_hash_algo_errors(PARAM_SET *set, ERR_TRCKR *err);

int sign_run(int argc, char** argv, char **envp) {
	int res;
	char buf[2048];
	PARAM_SET *set = NULL;
	TASK_SET *task_set = NULL;
	TASK *task = NULL;
	KSI_CTX *ksi = NULL;
	ERR_TRCKR *err = NULL;
	SMART_FILE *logfile = NULL;
	KSI_Signature *sig = NULL;
	int d = 0;
	int dump = 0;

	/**
	 * Extract command line parameters.
	 */
	res = PARAM_SET_new(
			CONF_generate_param_set_desc("{sign}{i}{o}{H}{data-out}{d}{dump}{log}{conf}{h|help}{dump-last-leaf}{prev-leaf}{no-masking}{masking-iv}{no-mdata}", "S", buf, sizeof(buf)),
			&set);
	if (res != KT_OK) goto cleanup;

	res = TASK_SET_new(&task_set);
	if (res != PST_OK) goto cleanup;

	res = generate_tasks_set(set, task_set);
	if (res != PST_OK) goto cleanup;

	res = TASK_INITIALIZER_getServiceInfo(set, argc, argv, envp);
	if (res != PST_OK) goto cleanup;

	res = TASK_INITIALIZER_check_analyze_report(set, task_set, 0.2, 0.1, &task);
	if (res != KT_OK) goto cleanup;

	res = TOOL_init_ksi(set, &ksi, &err, &logfile);
	if (res != KT_OK) goto cleanup;

	d = PARAM_SET_isSetByName(set, "d");
	dump = PARAM_SET_isSetByName(set, "dump");

	res = check_pipe_errors(set, err);
	if (res != KT_OK) goto cleanup;

	res = check_hash_algo_errors(set, err);
	if (res != KT_OK) goto cleanup;

	/**
	 * If everything OK, run the task.
	 */
	res = sign_save_to_file(set, ksi, err, &sig);
	if (res != KSI_OK) goto cleanup;

	/**
	 * If signature was created without errors print some info on demand.
	 */
	if (dump) {
		print_result("\n");
		OBJPRINT_signatureDump(sig, print_result);
	}

cleanup:
	print_progressResult(res);
	KSITOOL_KSI_ERRTrace_save(ksi);

	if (res != KT_OK) {
		KSITOOL_KSI_ERRTrace_LOG(ksi);

		print_errors("\n");
		if (d) 	ERR_TRCKR_printExtendedErrors(err);
		else 	ERR_TRCKR_printErrors(err);
	}

	SMART_FILE_close(logfile);
	KSI_Signature_free(sig);
	TASK_SET_free(task_set);
	PARAM_SET_free(set);
	ERR_TRCKR_free(err);
	KSI_CTX_free(ksi);

	return KSITOOL_errToExitCode(res);
}
char *sign_help_toString(char*buf, size_t len) {
	size_t count = 0;

	count += KSI_snprintf(buf + count, len - count,
		"Usage:\n"
		" %s sign -i <input> [-o <out.ksig>] -S <URL>\n"
		"         [--aggr-user <user> --aggr-key <key>] [-H <alg>] [--data-out <file>] [more_options]\n"
		"\n"
		"\n"
		" -i <input>\n"
		"           - The data is either the path to the file to be hashed and signed or\n"
		"             a hash imprint in case  the  data  to be signed has been hashed\n"
		"             already. Use '-' as file name to read data to be hashed from stdin.\n"
		"             Hash imprint format: <alg>:<hash in hex>.\n"
		" -o <out.ksig>\n"
		"           - Output file path for the signature. Use '-' as file name to\n"
		"             redirect signature binary stream to stdout. If not specified, the\n"
		"             signature is saved to <input file>.ksig (or <input file>_<nr>.ksig,\n"
		"             where <nr> is auto-incremented counter if the output file already\n"
		"             exists will always overwrite the existing file.\n"
		" -H <alg> \n"
		"           - Use the given hash algorithm to hash the file to be signed.\n"
		"             Use ksi -h to get the list of supported hash algorithms.\n"
		" -S <URL>  - Signing service (KSI Aggregator) URL.\n"
		" --aggr-user <str>\n"
		"           - Username for signing service.\n"
		" --aggr-key <str>\n"
		"           - HMAC key for signing service.\n"
		" --data-out <file>\n"
		"           - Save signed data to file. Use when signing an incoming stream.\n"
		"             Use '-' as file name to redirect data being hashed to stdout.\n"

		" --max-in-count <int>\n"
		"           - Set the maximum count of input files permitted (default 1024).\n"
		" --max-lvl <int>\n"
		"           - Set the maximum depth of the local aggregation tree (default 0).\n"
		" --sequential\n"
		"           - Enable signing of multiple files in sequence to avoid the local\n"
		" --max-aggr-rounds <int>\n"
		"           - Set the maximum count of local aggregation rounds (default 1).\n"
		" --dump-last-leaf\n"
		"           - Dump the last leaf of the local aggregation tree.\n"
		" --prev-leaf <hash>\n"
		"           - Specify the last hash value of the last local aggregation trees\n"
		"             leaf to link it with the first local aggregation tree (default \n"
		"             zero hash).\n"
		" --no-masking\n"
		"           - Disable masking of aggregations tree input leafs.\n"
		" --masking-iv <hex>\n"
		"           - Specify a hex string to initialize the masking process.\n"
		" --no-mdata\n"
		"           - No metadat will be embedded into the signature even if the metadata\n"
		"             is configured.\n"
		" --mdata-cli-id <str>\n"
		"           - Specify client id as a string that will be embedded into the\n"
		"             signature as metadata. It is mandatory for the metadata.\n"
		" --mdata-mac-id <str>\n"
		"           - Optional machine id as a string that will be embedded into the\n"
		"             signature as metadata.\n"
		" --mdata-sqn-nr <int>\n"
		"           - Optional sequence number of the request as integer that will be"
		"             embedded into the signature as metadata.\n"
		" --mdata-req-tm <int>\n"
		"           - Optional request time extracted from the machine clock that will be\n"
		"             embedded into signature as metadata.\n"

		" -d        - Print detailed information about processes and errors to stderr.\n"
		" --dump    - Dump signature created in human-readable format to stdout.\n"
		" --conf <file>\n"
		"           - Read configuration options from given file. It must be noted\n"
		"             that configuration options given explicitly on command line will\n"
		"             override the ones in the configuration file.\n"
		" --log <file>\n"
		"           - Write libksi log to given file. Use '-' as file name to redirect\n"
		"             log to stdout.\n"
		" --        - Disable command-line parameter parsing. After the parameter parsing\n"
		"             is disabled all tokens are interpreted as inputs.\n"
		, TOOL_getName()
	);

	return buf;
}

const char *sign_get_desc(void) {
	return "Signs the given input with KSI.";
}


static int sign_save_to_file(PARAM_SET *set, KSI_CTX *ksi, ERR_TRCKR *err, KSI_Signature **sig) {
	int res;
	int d = 0;
	KSI_DataHash *hash = NULL;
	KSI_Signature *tmp = NULL;
	KSI_HashAlgorithm algo = KSI_HASHALG_INVALID;
	char *outSigFileName = NULL;
	char *signed_data_out = NULL;
	COMPOSITE extra;
	const char *mode = NULL;
	char buf[1024];
	char real_output_name[1024];

	if(set == NULL || sig == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	/**
	 * Extract the signature output file name and signed data output file name if present.
	 * Set file save extra mode to 'i' as incremental write.
	 */
	if (!PARAM_SET_isSetByName(set, "o")) {
		mode = "i";
		outSigFileName = get_output_file_name_if_not_defined(set, err, buf, sizeof(buf));
		if (outSigFileName == NULL) {
			ERR_TRCKR_ADD(err, res = KT_UNKNOWN_ERROR, "Error: Unable to generate output file name.");
			goto cleanup;
		}
	} else {
		res = PARAM_SET_getStr(set, "o", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &outSigFileName);
		if (res != PST_OK && res != PST_PARAMETER_EMPTY) goto cleanup;
	}

	res = PARAM_SET_getStr(set, "data-out", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &signed_data_out);
	if (res != PST_OK && res != PST_PARAMETER_EMPTY) goto cleanup;

	d = PARAM_SET_isSetByName(set, "d");

	/**
	 * Extract the hash algorithm. If not specified, set algorithm as default.
	 * It must be noted that if hash is extracted from imprint, has algorithm has
	 * no effect.
	 */
	if (PARAM_SET_isSetByName(set, "H")) {
		res = PARAM_SET_getObjExtended(set, "H", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, NULL, (void**)&algo);
		if (res != PST_OK && res != PST_PARAMETER_EMPTY) goto cleanup;
	} else {
		algo = KSI_getHashAlgorithmByName("default");
	}

	/**
	 * Initialize helper data structure, retrieve the has and sign the hash value.
	 */
	extra.ctx = ksi;
	extra.err = err;
	extra.h_alg = &algo;
	extra.fname_out = signed_data_out;

	print_progressDesc(d, "Extracting hash from input... ");
	res = PARAM_SET_getObjExtended(set, "i", NULL, PST_PRIORITY_HIGHEST, 0, &extra, (void**)&hash);
	if (res != KT_OK) goto cleanup;
	print_progressResult(res);

	print_progressDesc(d, "Creating signature from hash... ");
	res = KSITOOL_createSignature(err, ksi, hash, &tmp);
	ERR_CATCH_MSG(err, res, "Error: Unable to create signature.");
	print_progressResult(res);


	/**
	 * Save KSI signature to file.
	 */
	res = KSI_OBJ_saveSignature(err, ksi, tmp, mode, outSigFileName, real_output_name, sizeof(real_output_name));
	if (res != KT_OK) goto cleanup;
	print_debug("Signature saved to '%s'.\n", real_output_name);

	*sig = tmp;
	tmp = NULL;
	res = KT_OK;

cleanup:
	print_progressResult(res);

	KSI_Signature_free(tmp);
	KSI_DataHash_free(hash);

	return res;
}

static int generate_tasks_set(PARAM_SET *set, TASK_SET *task_set) {
	int res;

	if (set == NULL || task_set == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	/**
	 * Configure parameter set, control, repair and object extractor function.
	 */
	res = CONF_initialize_set_functions(set, "S");
	if (res != KT_OK) goto cleanup;

	PARAM_SET_addControl(set, "{conf}", isFormatOk_inputFile, isContentOk_inputFileRestrictPipe, convertRepair_path, NULL);
	PARAM_SET_addControl(set, "{o}{data-out}{log}", isFormatOk_path, NULL, convertRepair_path, NULL);
	PARAM_SET_addControl(set, "{i}", isFormatOk_inputHash, isContentOk_inputHash, NULL, extract_inputHash);
	PARAM_SET_addControl(set, "{H}", isFormatOk_hashAlg, isContentOk_hashAlg, NULL, extract_hashAlg);
	PARAM_SET_addControl(set, "{prev-leaf}", isFormatOk_imprint, isContentOk_imprint, NULL, extract_imprint);
	PARAM_SET_addControl(set, "{masking-iv}", isFormatOk_hex, NULL, NULL, extract_OctetString);
	PARAM_SET_addControl(set, "{d}{dump}{dump-last-leaf}{no-masking}{no-mdata}", isFormatOk_flag, NULL, NULL, NULL);

	/**
	 * Make the parameter -i collect:
	 * 1) All values that are exactly after -i (both a and -i are collected -i a, -i -i)
	 * 2) all values that are not potential parameters (unknown / typo) parameters (will ignore -q, --test)
	 * 3) All values that are specified after --.
	 */
	PARAM_SET_setParseOptions(set, "i", PST_PRSCMD_HAS_VALUE | PST_PRSCMD_COLLECT_LOOSE_VALUES | PST_PRSCMD_COLLECT_LOOSE_PERMIT_END_OF_COMMANDS);

	/*					  ID	DESC										MAN			 ATL	FORBIDDEN	IGN	*/
	TASK_SET_add(task_set, 0,	"Sign data.",								"S,i",	 NULL,	"H,data-out",		NULL);
	TASK_SET_add(task_set, 1,	"Sign data, specify hash alg.",				"S,i,H",	 NULL,	"data-out",		NULL);
	TASK_SET_add(task_set, 2,	"Sign and save data.",						"S,i,data-out",	 NULL,	"H",		NULL);
	TASK_SET_add(task_set, 3,	"Sign and save data, specify hash alg.",	"S,i,H,data-out", NULL,	NULL,		NULL);

cleanup:

	return res;
}

static char* get_output_file_name_if_not_defined(PARAM_SET *set, ERR_TRCKR *err, char *buf, size_t buf_len) {
	char *ret = NULL;
	int res;
	char *in_file_name = NULL;
	char hash_algo[1024];
	char *colon = NULL;

	if (set == NULL || err == NULL || buf == NULL || buf_len == 0) goto cleanup;

	res = PARAM_SET_getStr(set, "i", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &in_file_name);
	if (res != PST_OK) goto cleanup;

	if (strcmp(in_file_name, "-") == 0) {
		KSI_snprintf(buf, buf_len, "stdin.ksig");
	} else if (is_imprint(in_file_name)) {
		/* Search for the algorithm name. */
		KSI_strncpy(hash_algo, in_file_name, sizeof(hash_algo));
		colon = strchr(hash_algo, ':');

		/* Create the file name from hash algorithm. */
		if (colon != NULL) {
			*colon = '\0';
			KSI_snprintf(buf, buf_len, "%s.ksig", hash_algo);
		} else {
			KSI_snprintf(buf, buf_len, "hash_imprint.ksig");
		}
	} else {
		KSI_snprintf(buf, buf_len, "%s.ksig", in_file_name);
	}

	ret = buf;

cleanup:

	return ret;
}


static int check_pipe_errors(PARAM_SET *set, ERR_TRCKR *err) {
	int res;

	res = get_pipe_out_error(set, err, "o,data-out", "dump");
	if (res != KT_OK) goto cleanup;

	res = get_pipe_out_error(set, err, "o,data-out,log", NULL);
	if (res != KT_OK) goto cleanup;

cleanup:
	return res;
}

static int check_hash_algo_errors(PARAM_SET *set, ERR_TRCKR *err) {
	int res;
	char *i_value = NULL;

	res = PARAM_SET_getStr(set, "i", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &i_value);
	if (res != KT_OK) goto cleanup;

	if (PARAM_SET_isSetByName(set, "H") && is_imprint(i_value)) {
		ERR_TRCKR_ADD(err, res = KT_INVALID_CMD_PARAM, "Error: Unable to use -H and -i together as input is hash imprint.");
		goto cleanup;
	}

	res = KT_OK;

cleanup:

	return res;
}
