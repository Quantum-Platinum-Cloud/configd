/*
 * Copyright (c) 2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 *
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include "configd.h"
#include "session.h"

SCDStatus
_SCDTouch(SCDSessionRef session, CFStringRef key)
{
	SCDSessionPrivateRef	sessionPrivate = (SCDSessionPrivateRef)session;
	SCDStatus		scd_status;
	boolean_t		wasLocked;
	SCDHandleRef		handle;
	CFPropertyListRef	value;

	SCDLog(LOG_DEBUG, CFSTR("_SCDTouch:"));
	SCDLog(LOG_DEBUG, CFSTR("  key = %@"), key);

	if ((session == NULL) || (sessionPrivate->server == MACH_PORT_NULL)) {
		return SCD_NOSESSION;		/* you can't do anything with a closed session */
	}

	/*
	 * 1. Determine if the cache lock is currently held by this session
	 *    and acquire the lock if necessary.
	 */
	wasLocked = SCDOptionGet(NULL, kSCDOptionIsLocked);
	if (!wasLocked) {
		scd_status = _SCDLock(session);
		if (scd_status != SCD_OK) {
			SCDLog(LOG_DEBUG, CFSTR("  _SCDLock(): %s"), SCDError(scd_status));
			return scd_status;
		}
	}

	/*
	 * 2. Grab the current (or establish a new) cache entry for this key.
	 */
	scd_status = _SCDGet(session, key, &handle);
	switch (scd_status) {
		case SCD_NOKEY :
			/* cache entry does not exist, create */
			handle = SCDHandleInit();
			value  = CFDateCreate(NULL, CFAbsoluteTimeGetCurrent());
			SCDLog(LOG_DEBUG, CFSTR("  new time stamp = %@"), value);
			SCDHandleSetData(handle, value);
			CFRelease(value);
			break;

		case SCD_OK :
			/* cache entry exists, update */
			value = SCDHandleGetData(handle);
			if (CFGetTypeID(value) == CFDateGetTypeID()) {
				/* if value is a CFDate */
				value = CFDateCreate(NULL, CFAbsoluteTimeGetCurrent());
				SCDLog(LOG_DEBUG, CFSTR("  new time stamp = %@"), value);
				SCDHandleSetData(handle, value);
				CFRelease(value);
			} /* else, we'll just save the data (again) to bump the instance */
			break;

		default :
			SCDLog(LOG_DEBUG, CFSTR("  _SCDGet(): %s"), SCDError(scd_status));
			goto done;
	}

	scd_status = _SCDSet(session, key, handle);
	SCDHandleRelease(handle);

    done :

	/*
	 * 8. Release the lock if we acquired it as part of this request.
	 */
	if (!wasLocked)
		_SCDUnlock(session);

	return SCD_OK;
}


kern_return_t
_configtouch(mach_port_t 			server,
	     xmlData_t			keyRef,		/* raw XML bytes */
	     mach_msg_type_number_t	keyLen,
	     int				*scd_status
)
{
	kern_return_t		status;
	serverSessionRef	mySession = getSession(server);
	CFDataRef		xmlKey;		/* key  (XML serialized) */
	CFStringRef		key;		/* key  (un-serialized) */
	CFStringRef		xmlError;

	SCDLog(LOG_DEBUG, CFSTR("Touch key in configuration database."));
	SCDLog(LOG_DEBUG, CFSTR("  server = %d"), server);

	/* un-serialize the key */
	xmlKey = CFDataCreate(NULL, keyRef, keyLen);
	status = vm_deallocate(mach_task_self(), (vm_address_t)keyRef, keyLen);
	if (status != KERN_SUCCESS) {
		SCDLog(LOG_DEBUG, CFSTR("vm_deallocate(): %s"), mach_error_string(status));
		/* non-fatal???, proceed */
	}
	key = CFPropertyListCreateFromXMLData(NULL,
					      xmlKey,
					      kCFPropertyListImmutable,
					      &xmlError);
	CFRelease(xmlKey);
	if (xmlError) {
		SCDLog(LOG_DEBUG, CFSTR("CFPropertyListCreateFromXMLData() key: %s"), xmlError);
		*scd_status = SCD_FAILED;
		return KERN_SUCCESS;
	}

	*scd_status = _SCDTouch(mySession->session, key);
	CFRelease(key);

	return KERN_SUCCESS;
}
