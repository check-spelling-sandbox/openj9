/*******************************************************************************
 * Copyright (c) 2021, 2021 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/
#if defined(LINUX)
#include <criu/criu.h>
#include <fcntl.h>
#include <errno.h>
#endif /* defined(LINUX) */

#include "jni.h"
#include "j9.h"
#include "j9jclnls.h"
#include "criusupport.h"
#include "ut_j9criu.h"

#define STRING_BUFFER_SIZE 256

static void
setupJNIFieldIDs(JNIEnv *env)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	jclass criuJVMCheckpointExceptionClass = NULL;
	jclass criuSystemCheckpointExceptionClass = NULL;
	jclass criuRestoreExceptionClass = NULL;

	criuJVMCheckpointExceptionClass = (*env)->FindClass(env, "org/eclipse/openj9/criu/JVMCheckpointException");
	Assert_CRIU_notNull(criuJVMCheckpointExceptionClass);
	vm->criuJVMCheckpointExceptionClass = (*env)->NewGlobalRef(env, criuJVMCheckpointExceptionClass);

	vm->criuJVMCheckpointExceptionInit = (*env)->GetMethodID(env, criuJVMCheckpointExceptionClass, "<init>", "(Ljava/lang/String;I)V");
	Assert_CRIU_notNull(vm->criuJVMCheckpointExceptionInit);

	criuSystemCheckpointExceptionClass = (*env)->FindClass(env, "org/eclipse/openj9/criu/SystemCheckpointException");
	Assert_CRIU_notNull(criuSystemCheckpointExceptionClass);
	vm->criuSystemCheckpointExceptionClass = (*env)->NewGlobalRef(env, criuSystemCheckpointExceptionClass);

	vm->criuSystemCheckpointExceptionInit = (*env)->GetMethodID(env, criuSystemCheckpointExceptionClass, "<init>", "(Ljava/lang/String;I)V");
	Assert_CRIU_notNull(vm->criuSystemCheckpointExceptionInit);

	criuRestoreExceptionClass = (*env)->FindClass(env, "org/eclipse/openj9/criu/RestoreException");
	Assert_CRIU_notNull(criuRestoreExceptionClass);
	vm->criuRestoreExceptionClass = (*env)->NewGlobalRef(env, criuRestoreExceptionClass);

	vm->criuRestoreExceptionInit = (*env)->GetMethodID(env, criuRestoreExceptionClass, "<init>", "(Ljava/lang/String;I)V");
	Assert_CRIU_notNull(vm->criuRestoreExceptionInit);

	if (NULL == vm->criuJVMCheckpointExceptionClass
		|| NULL == vm->criuSystemCheckpointExceptionClass
		|| NULL == vm->criuRestoreExceptionClass
	) {
		vmFuncs->internalEnterVMFromJNI(currentThread);
		vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
		vmFuncs->internalExitVMToJNI(currentThread);
	}
}

jboolean JNICALL
Java_org_eclipse_openj9_criu_CRIUSupport_isCRIUSupportEnabledImpl(JNIEnv *env, jclass unused)
{
	J9VMThread *currentThread = (J9VMThread *) env;
	J9JavaVM *vm = currentThread->javaVM;
	jboolean res = JNI_FALSE;

	if (vm->internalVMFunctions->isCRIUSupportEnabled(currentThread)) {
#if defined(LINUX)
		if (0 == criu_init_opts()) {
			res = JNI_TRUE;
		}
#endif /* defined(LINUX) */
	}
	setupJNIFieldIDs(env);

	return res;
}

jboolean JNICALL
Java_org_eclipse_openj9_criu_CRIUSupport_isCheckpointAllowed(JNIEnv *env, jclass unused)
{
	J9VMThread *currentThread = (J9VMThread *) env;
	jboolean res = JNI_FALSE;

	if (currentThread->javaVM->internalVMFunctions->isCheckpointAllowed(currentThread)) {
		res = JNI_TRUE;
	}

	return res;
}

#define J9_NATIVE_STRING_NO_ERROR 0
#define J9_NATIVE_STRING_OUT_OF_MEMORY (-1)
#define J9_NATIVE_STRING_FAIL_TO_CONVERT (-2)

/**
 * Converts the given java string to native representation. The nativeString parameter should point
 * to a buffer with its size specified by nativeStringBufSize. If the java string length exceeds the buffer
 * size, nativeString will be set to allocated memory.
 *
 * @note If successful, the caller is responsible for freeing any memory allocated and stored in nativeString.
 *
 * @param[in] currentThread current thread
 * @param[in] javaString java string object
 * @param[out] nativeString the native representation of the java string
 * @param[in] nativeStringBufSize size of the nativeString buffer
 *
 * @return return code indicating success, allocation failure, or string conversion failure
 */
static IDATA
getNativeString(J9VMThread *currentThread, j9object_t javaString, char **nativeString, IDATA nativeStringBufSize)
{
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	char mutf8StringBuf[STRING_BUFFER_SIZE];
	char *mutf8String = NULL;
	UDATA mutf8StringSize = 0;
	IDATA requiredConvertedStringSize = 0;
	char *localNativeString = *nativeString;
	IDATA res = J9_NATIVE_STRING_NO_ERROR;
	PORT_ACCESS_FROM_VMC(currentThread);
	OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

	mutf8String = vmFuncs->copyStringToUTF8WithMemAlloc(currentThread, javaString, J9_STR_NULL_TERMINATE_RESULT, "", 0, mutf8StringBuf, STRING_BUFFER_SIZE, &mutf8StringSize);
	if (NULL == mutf8String) {
		res = J9_NATIVE_STRING_OUT_OF_MEMORY;
		goto free;
	}

	/* get the required size */
	requiredConvertedStringSize = omrstr_convert(J9STR_CODE_MUTF8, J9STR_CODE_PLATFORM_RAW,
					mutf8String,
					mutf8StringSize,
					localNativeString,
					0);

	if (requiredConvertedStringSize < 0) {
		Trc_CRIU_getNativeString_getStringSizeFail(currentThread, mutf8String, mutf8StringSize);
		res = J9_NATIVE_STRING_FAIL_TO_CONVERT;
		goto free;
	}

	/* Add 1 for NUL terminator */
	requiredConvertedStringSize += 1;

	if (requiredConvertedStringSize > nativeStringBufSize) {
		localNativeString = j9mem_allocate_memory(requiredConvertedStringSize, OMRMEM_CATEGORY_VM);
		if (NULL == localNativeString) {
			res = J9_NATIVE_STRING_OUT_OF_MEMORY;
			goto free;
		}
	}

	(localNativeString)[requiredConvertedStringSize - 1] = '\0';

	/* convert the string */
	requiredConvertedStringSize = omrstr_convert(J9STR_CODE_MUTF8, J9STR_CODE_PLATFORM_RAW,
					mutf8String,
					mutf8StringSize,
					localNativeString,
					requiredConvertedStringSize);

	if (requiredConvertedStringSize < 0) {
		Trc_CRIU_getNativeString_convertFail(currentThread, mutf8String, mutf8StringSize, requiredConvertedStringSize);
		res = J9_NATIVE_STRING_FAIL_TO_CONVERT;
		goto free;
	}

free:
	if (mutf8String != mutf8StringBuf) {
		j9mem_free_memory(mutf8String);
	}
	if (localNativeString != *nativeString) {
		if (J9_NATIVE_STRING_NO_ERROR == res) {
			*nativeString = localNativeString;
		} else {
			j9mem_free_memory(localNativeString);
			localNativeString = NULL;
		}
	}

	return res;
}

void JNICALL
Java_org_eclipse_openj9_criu_CRIUSupport_checkpointJVMImpl(JNIEnv *env,
		jclass unused,
		jstring imagesDir,
		jboolean leaveRunning,
		jboolean shellJob,
		jboolean extUnixSupport,
		jint logLevel,
		jstring logFile,
		jboolean fileLocks,
		jstring workDir,
		jboolean tcpEstablished,
		jboolean autoDedup,
		jboolean trackMemory)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;

	jclass currentExceptionClass = NULL;
	char *exceptionMsg = NULL;
	const char *nlsMsgFormat = NULL;
	UDATA msgCharLength = 0;
	IDATA systemReturnCode = 0;
	PORT_ACCESS_FROM_VMC(currentThread);

	if (vmFuncs->isCheckpointAllowed(currentThread)) {
#if defined(LINUX)
		j9object_t cpDir = NULL;
		j9object_t log = NULL;
		j9object_t wrkDir = NULL;
		IDATA dirFD = 0;
		IDATA workDirFD = 0;
		char directoryBuf[STRING_BUFFER_SIZE];
		char *directoryChars = directoryBuf;
		char logFileBuf[STRING_BUFFER_SIZE];
		char *logFileChars = logFileBuf;
		char workDirBuf[STRING_BUFFER_SIZE];
		char *workDirChars = workDirBuf;
		BOOLEAN isAfterCheckpoint = FALSE;

		vmFuncs->internalEnterVMFromJNI(currentThread);

		Assert_CRIU_notNull(imagesDir);
		cpDir = J9_JNI_UNWRAP_REFERENCE(imagesDir);
		systemReturnCode = getNativeString(currentThread, cpDir, &directoryChars, STRING_BUFFER_SIZE);
		switch (systemReturnCode) {
		case J9_NATIVE_STRING_NO_ERROR:
			break;
		case J9_NATIVE_STRING_OUT_OF_MEMORY:
			vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
			goto freeDir;
		case J9_NATIVE_STRING_FAIL_TO_CONVERT:
			currentExceptionClass = vm->criuJVMCheckpointExceptionClass;
			nlsMsgFormat = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_JCL_CRIU_FAILED_TO_CONVERT_JAVA_STRING, NULL);
			goto freeDir;
		}

		if (NULL != logFile) {
			log = J9_JNI_UNWRAP_REFERENCE(logFile);
			systemReturnCode = getNativeString(currentThread, log, &logFileChars, STRING_BUFFER_SIZE);
			switch (systemReturnCode) {
			case J9_NATIVE_STRING_NO_ERROR:
				break;
			case J9_NATIVE_STRING_OUT_OF_MEMORY:
				vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
				goto freeLog;
			case J9_NATIVE_STRING_FAIL_TO_CONVERT:
				currentExceptionClass = vm->criuJVMCheckpointExceptionClass;
				nlsMsgFormat = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_JCL_CRIU_FAILED_TO_CONVERT_JAVA_STRING, NULL);
				goto freeLog;
			}
		}

		if (NULL != workDir) {
			wrkDir = J9_JNI_UNWRAP_REFERENCE(workDir);
			systemReturnCode = getNativeString(currentThread, wrkDir, &workDirChars, STRING_BUFFER_SIZE);
			switch (systemReturnCode) {
			case J9_NATIVE_STRING_NO_ERROR:
				break;
			case J9_NATIVE_STRING_OUT_OF_MEMORY:
				vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
				goto freeWorkDir;
			case J9_NATIVE_STRING_FAIL_TO_CONVERT:
				currentExceptionClass = vm->criuJVMCheckpointExceptionClass;
				nlsMsgFormat = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_JCL_CRIU_FAILED_TO_CONVERT_JAVA_STRING, NULL);
				goto freeWorkDir;
			}
		}

		dirFD = open(directoryChars, O_DIRECTORY);
		if (dirFD < 0) {
			systemReturnCode = errno;
			currentExceptionClass = vm->criuJVMCheckpointExceptionClass;
			nlsMsgFormat = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_JCL_CRIU_FAILED_TO_OPEN_DIR, NULL);
			goto freeWorkDir;
		}

		if (NULL != workDir) {
			workDirFD = open(workDirChars, O_DIRECTORY);
			if (workDirFD < 0) {
				systemReturnCode = errno;
				currentExceptionClass = vm->criuJVMCheckpointExceptionClass;
				nlsMsgFormat = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_JCL_CRIU_FAILED_TO_OPEN_WORK_DIR, NULL);
				goto closeDirFD;
			}
		}

		systemReturnCode = criu_init_opts();
		if (0 != systemReturnCode) {
			currentExceptionClass = vm->criuSystemCheckpointExceptionClass;
			nlsMsgFormat = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_JCL_CRIU_INIT_FAILED, NULL);
			goto closeWorkDirFD;
		}

		criu_set_images_dir_fd(dirFD);
		criu_set_shell_job(JNI_FALSE != shellJob);
		if (logLevel > 0) {
			criu_set_log_level((int)logLevel);
		}
		if (NULL != logFile) {
			criu_set_log_file(logFileChars);
		}
		criu_set_leave_running(JNI_FALSE != leaveRunning);
		criu_set_ext_unix_sk(JNI_FALSE != extUnixSupport);
		criu_set_file_locks(JNI_FALSE != fileLocks);
		criu_set_tcp_established(JNI_FALSE != tcpEstablished);
		criu_set_auto_dedup(JNI_FALSE != autoDedup);
		criu_set_track_mem(JNI_FALSE != trackMemory);

		if (NULL != workDir) {
			criu_set_work_dir_fd(workDirFD);
		}

		vmFuncs->acquireExclusiveVMAccess(currentThread);

		if (FALSE == vmFuncs->jvmCheckpointHooks(currentThread)) {
			goto releaseExclusive;
		}

		systemReturnCode = criu_dump();
		if (systemReturnCode < 0) {
			currentExceptionClass = vm->criuSystemCheckpointExceptionClass;
			nlsMsgFormat = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_JCL_CRIU_DUMP_FAILED, NULL);
			goto releaseExclusive;
		}

		/* We can only end up here if the CRIU restore was successful */
		isAfterCheckpoint = TRUE;

		if (FALSE == vmFuncs->jvmRestoreHooks(currentThread)) {
			goto releaseExclusive;
		}

releaseExclusive:
		vmFuncs->releaseExclusiveVMAccess(currentThread);
closeWorkDirFD:
		if ((0 != close(workDirFD)) && (NULL == currentExceptionClass)) {
			systemReturnCode = errno;
			if (isAfterCheckpoint) {
				currentExceptionClass = vm->criuRestoreExceptionClass;
			} else {
				currentExceptionClass = vm->criuSystemCheckpointExceptionClass;
			}
			nlsMsgFormat = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_JCL_CRIU_FAILED_TO_CLOSE_WORK_DIR, NULL);
		}
closeDirFD:
		if ((0 != close(dirFD)) && (NULL == currentExceptionClass)) {
			systemReturnCode = errno;
			if (isAfterCheckpoint) {
				currentExceptionClass = vm->criuRestoreExceptionClass;
			} else {
				currentExceptionClass = vm->criuSystemCheckpointExceptionClass;
			}
			nlsMsgFormat = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_JCL_CRIU_FAILED_TO_CLOSE_DIR, NULL);
		}
freeWorkDir:
		if (workDirBuf != workDirChars) {
			j9mem_free_memory(workDirChars);
		}
freeLog:
		if (logFileBuf != logFileChars) {
			j9mem_free_memory(logFileChars);
		}
freeDir:
		if (directoryBuf != directoryChars) {
			j9mem_free_memory(directoryChars);
		}

		vmFuncs->internalExitVMToJNI(currentThread);
#endif /* defined(LINUX) */
	}

	if (NULL != currentExceptionClass) {
		msgCharLength = j9str_printf(PORTLIB, NULL, 0, nlsMsgFormat, systemReturnCode);
		exceptionMsg = j9mem_allocate_memory(msgCharLength, J9MEM_CATEGORY_VM);

		j9str_printf(PORTLIB, exceptionMsg, msgCharLength, nlsMsgFormat, systemReturnCode);

		jmethodID init = NULL;
		if (vm->criuJVMCheckpointExceptionClass == currentExceptionClass) {
			init = vm->criuJVMCheckpointExceptionInit;
		} else if (vm->criuSystemCheckpointExceptionClass == currentExceptionClass) {
			init = vm->criuSystemCheckpointExceptionInit;
		} else {
			init = vm->criuRestoreExceptionInit;
		}
		jstring jExceptionMsg = (*env)->NewStringUTF(env, exceptionMsg);

		if (JNI_FALSE == (*env)->ExceptionCheck(env)) {
			jobject exception = (*env)->NewObject(env, currentExceptionClass, init, jExceptionMsg, (jint)systemReturnCode);
			if (NULL != exception) {
				(*env)->Throw(env, (jthrowable)exception);
			}
		}

		if (NULL != exceptionMsg) {
			j9mem_free_memory(exceptionMsg);
		}
	}
}
