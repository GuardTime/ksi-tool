#include "gt_task_support.h"
#include "try-catch.h"

bool GT_extendTask(Task *task) {
	KSI_CTX *ksi = NULL;
	KSI_Signature *sig = NULL;
	KSI_Signature *ext = NULL;
	bool state = true;
	
	KSI_Integer *signTime = NULL;
	KSI_Integer *pubTime = NULL;
	KSI_ExtendReq *extReq = NULL;
	KSI_ExtendResp *extResp = NULL;
	KSI_CalendarHashChain *calHashChain = NULL;
	KSI_Integer *respStatus = NULL;
	KSI_PublicationRecord *pubRec = NULL;
	KSI_PublicationRecord *pubRecClone = NULL;
	KSI_PublicationsFile *pubFile = NULL;
	KSI_RequestHandle *handle = NULL;
	
	bool T,t, n, r, d;
	char *inSigFileName = NULL;
	char *outSigFileName = NULL;
	int publicationTime = 0;
	
	paramSet_getStrValueByNameAt(task->set, "i", 0,&inSigFileName);
	paramSet_getStrValueByNameAt(task->set, "o", 0,&outSigFileName);
	T = paramSet_getIntValueByNameAt(task->set,"T",0,&publicationTime);
	n = paramSet_isSetByName(task->set, "n");
	t = paramSet_isSetByName(task->set, "t");
	r = paramSet_isSetByName(task->set, "r");
	d = paramSet_isSetByName(task->set, "d");
	
	resetExceptionHandler();
	try
		CODE{
			/*Initialization of KSI */
			initTask_throws(task, &ksi);
			/* Read the signature. */
			printf("Reading signature...");
			KSI_Signature_fromFile_throws(ksi, inSigFileName, &sig);
			printf("ok.\n");

			/* Make sure the signature is ok. */
			printf("Verifying old signature...");
			MEASURE_TIME(KSI_verifySignature(ksi, sig));
			printf("ok. %s\n",t ? str_measuredTime() : "");

			/* Extend the signature. */
			if(T){
				printf("Extending old signature to %i...", publicationTime);

				KSI_Signature_clone_throws(sig, &ext);
				KSI_Signature_getSigningTime_throws(ext, &signTime);
				KSI_Integer_new_throws(ksi, publicationTime, &pubTime);

				KSI_ExtendReq_new_throws(ksi, &extReq);
				KSI_ExtendReq_setAggregationTime_throws(extReq, signTime);
				KSI_ExtendReq_setPublicationTime_throws(extReq, pubTime);
				
				
				/* Send the actual request. */
				measureLastCall();
				KSI_sendExtendRequest_throws(ksi, extReq, &handle);
				KSI_RequestHandle_getExtendResponse_throws(handle, &extResp);

				/* Verify the response is ok. */
				KSI_ExtendResp_getStatus_throws(extResp, &respStatus);

				if (respStatus == NULL || !KSI_Integer_equalsUInt(respStatus, 0)) {
					KSI_Utf8String *errm = NULL;
					int res = KSI_ExtendResp_getErrorMsg(extResp, &errm);
					if (res == KSI_OK && KSI_Utf8String_cstr(errm) != NULL) {
						THROW_MSG(KSI_EXCEPTION, "Extender returned error %llu: '%s'.\n", (unsigned long long)KSI_Integer_getUInt64(respStatus), KSI_Utf8String_cstr(errm));
					}else{
						THROW_MSG(KSI_EXCEPTION, "Extender returned error %llu.\n", (unsigned long long)KSI_Integer_getUInt64(respStatus));
					}
				}
				measureLastCall();

				/*Remove HashChain from response. Add the hash chain to the signature.*/
				KSI_ExtendResp_getCalendarHashChain_throws(extResp, &calHashChain);
				KSI_ExtendResp_setCalendarHashChain_throws(extResp, NULL);
				KSI_Signature_replaceCalendarChain_throws(ext, calHashChain);
				
				/* If the publication exists set it as the trust anchor. */
				KSI_receivePublicationsFile_throws(ksi, &pubFile);
				
				/*TODO NB! pubRec must be cloned. It still belongs to the publications file*/
				KSI_PublicationsFile_getPublicationDataByTime_throws(pubFile, pubTime, &pubRec);
				if(pubRec != NULL){
					KSI_PublicationRecord_clone_throws(pubRec, &pubRecClone);
				}
				KSI_Signature_replacePublicationRecord_throws(ext, pubRecClone);
				pubRecClone = NULL;
			}
			else{
				printf("Extending old signature...");
				MEASURE_TIME(KSI_extendSignature_throws(ksi, sig, &ext));
			}
			printf("ok. %s\n",t ? str_measuredTime() : "");

			printf("Verifying extended signature...");
			MEASURE_TIME(KSI_Signature_verify_throws(ext, ksi));
			printf("ok. %s\n",t ? str_measuredTime() : "");
			
			/* Save signature. */
			saveSignatureFile_throws(ext, outSigFileName);
			printf("Extended signature saved.\n");
		}
		CATCH_ALL{
			printf("failed.\n");
			printErrorMessage();
			exceptionSolved();
			state = false;
		}
	end_try

	if(n || r || d) printf("\n");
	if (n) printSignerIdentity(ext);
	if (r) printSignaturePublicationReference(ext);
	if (d) printSignatureVerificationInfo(ext);

	KSI_Signature_free(sig);
	KSI_Signature_free(ext);
	
	KSI_ExtendReq_free(extReq);
	KSI_ExtendResp_free(extResp);
	KSI_RequestHandle_free(handle);
	KSI_PublicationRecord_free(pubRecClone);

	
	KSI_CTX_free(ksi);
	return state;
}


