/*[INCLUDE-IF CRIU_SUPPORT]*/
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
package org.eclipse.openj9.criu;

import java.nio.file.Path;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.nio.file.Files;
import java.util.Objects;
import java.io.File;

/**
 * CRIU Support API
 */
public final class CRIUSupport {

	static {
		try {
			AccessController.doPrivileged((PrivilegedAction<Void>) () -> {
				System.loadLibrary("j9criu29"); //$NON-NLS-1$
				return null;
			});
		} catch (UnsatisfiedLinkError e) {
			throw new InternalError(e);
		}
	}

	private static final CRIUDumpPermission CRIU_DUMP_PERMISSION = new CRIUDumpPermission();

	private static final boolean criuSupportEnabled = isCRIUSupportEnabledImpl();

	private static native boolean isCRIUSupportEnabledImpl();

	private static native boolean isCheckpointAllowed();

	private static native void checkpointJVMImpl(String imageDir,
			boolean leaveRunning,
			boolean shellJob,
			boolean extUnixSupport,
			int logLevel,
			String logFile,
			boolean fileLocks,
			String workDir,
			boolean tcpEstablished,
			boolean autoDedup,
			boolean trackMemory);

	/**
	 * Constructs a new {@code CRIUSupport}.
	 *
	 * The default CRIU dump options are:
	 * <p>{@code imageDir} = imageDir, the directory where the images are to be created.
	 * <p>{@code leaveRunning} = false
	 * <p>{@code shellJob} = false
	 * <p>{@code extUnixSupport} = false
	 * <p>{@code logLevel} = 2
	 * <p>{@code logFile} = criu.log
	 * <p>{@code fileLocks} = false
	 * <p>{@code workDir} = imageDir, the directory where the images are to be created.
	 *
	 * @param imageDir the directory that will hold the dump files as a java.nio.file.Path
	 * @throws NullPointerException if imageDir is null
	 * @throws SecurityException if no permission to access imageDir or no CRIU_DUMP_PERMISSION
	 * @throws IllegalArgumentException if imageDir is not a valid directory
	 */
	public CRIUSupport(Path imageDir) {
		SecurityManager manager = System.getSecurityManager();
		if (manager != null) {
			manager.checkPermission(CRIU_DUMP_PERMISSION);
		}

		setImageDir(imageDir);
	}

	/**
	 * Queries if CRIU support is enabled.
	 *
	 * @return TRUE is support is enabled, FALSE otherwise
	 */
	public static boolean isCRIUSupportEnabled() {
		return criuSupportEnabled;
	}

	private String imageDir;
	private boolean leaveRunning;
	private boolean shellJob;
	private boolean extUnixSupport;
	private int logLevel;
	private String logFile;
	private boolean fileLocks;
	private String workDir;
	private boolean tcpEstablished;
	private boolean autoDedup;
	private boolean trackMemory;

	/**
	 * Sets the directory that will hold the images upon checkpoint.
	 * This must be set before calling {@link #checkpointJVM()}.
	 *
	 * @param imageDir the directory as a java.nio.file.Path
	 * @return this
	 * @throws NullPointerException if imageDir is null
	 * @throws SecurityException if no permission to access imageDir
	 * @throws IllegalArgumentException if imageDir is not a valid directory
	 */
	public CRIUSupport setImageDir(Path imageDir) {
		Objects.requireNonNull(imageDir, "Image directory cannot be null");
		if (!Files.isDirectory(imageDir)) {
			throw new IllegalArgumentException("imageDir is not a valid directory");
		}
		String dir = imageDir.toAbsolutePath().toString();

		SecurityManager manager = System.getSecurityManager();
		if (manager != null) {
			manager.checkWrite(dir);
		}

		this.imageDir = dir;
		return this;
	}

	/**
	 * Controls whether process trees are left running after checkpoint.
	 * <p>Default: false
	 *
	 * @param leaveRunning
	 * @return this
	 */
	public CRIUSupport setLeaveRunning(boolean leaveRunning) {
		this.leaveRunning = leaveRunning;
		return this;
	}

	/**
	 * Controls ability to dump shell jobs.
	 * <p>Default: false
	 *
	 * @param shellJob
	 * @return this
	 */
	public CRIUSupport setShellJob(boolean shellJob) {
		this.shellJob = shellJob;
		return this;
	}

	/**
	 * Controls whether to dump only one end of a unix socket pair.
	 * <p>Default: false
	 *
	 * @param extUnixSupport
	 * @return this
	 */
	public CRIUSupport setExtUnixSupport(boolean extUnixSupport) {
		this.extUnixSupport = extUnixSupport;
		return this;
	}

	/**
	 * Sets the verbosity of log output.
	 * Available levels:
	 * <ol>
	 * <li>Only errors
	 * <li>Errors and warnings
	 * <li>Above + information messages and timestamps
	 * <li>Above + debug
	 * </ol>
	 * <p>Default: 2
	 *
	 * @param logLevel verbosity from 1 to 4 inclusive
	 * @return this
	 * @throws IllegalArgumentException if logLevel is not valid
	 */
	public CRIUSupport setLogLevel(int logLevel) {
		if (logLevel > 0 && logLevel <= 4) {
			this.logLevel = logLevel;
		} else {
			throw new IllegalArgumentException("Log level must be 1 to 4 inclusive");
		}
		return this;
	}

	/**
	 * Write log output to logFile.
	 * <p>Default: criu.log
	 *
	 * @param logFile name of the file to write log output to. The path to the file can be set with {@link #setWorkDir(Path)}.
	 * @return this
	 * @throws IllegalArgumentException if logFile is null or a path
	 */
	public CRIUSupport setLogFile(String logFile) {
		if (logFile != null && !logFile.contains(File.separator)) {
			this.logFile = logFile;
		} else {
			throw new IllegalArgumentException("Log file must not be null and not be a path");
		}
		return this;
	}

	/**
	 * Controls whether to dump file locks.
	 * <p>Default: false
	 *
	 * @param fileLocks
	 * @return this
	 */
	public CRIUSupport setFileLocks(boolean fileLocks) {
		this.fileLocks = fileLocks;
		return this;
	}

	/**
	 * Controls whether to re-establish TCP connects.
	 * <p>Default: false
	 *
	 * @param tcpEstablished
	 * @return this
	 */
	public CRIUSupport setTCPEstablished(boolean tcpEstablished) {
		this.tcpEstablished = tcpEstablished;
		return this;
	}

	/**
	 * Controls whether auto dedup of memory pages is enabled.
	 * <p>Default: false
	 *
	 * @param autoDedup
	 * @return this
	 */
	public CRIUSupport setAutoDedup(boolean autoDedup) {
		this.autoDedup = autoDedup;
		return this;
	}

	/**
	 * Controls whether memory tracking is enabled.
	 * <p>Default: false
	 *
	 * @param trackMemory
	 * @return this
	 */
	public CRIUSupport setTrackMemory(boolean trackMemory) {
		this.trackMemory = trackMemory;
		return this;
	}

	/**
	 * Sets the directory where non-image files are stored (e.g. logs).
	 * <p>Default: same as path set by {@link #setImageDir(Path)}.
	 *
	 * @param workDir the directory as a java.nio.file.Path
	 * @return this
	 * @throws NullPointerException if workDir is null
	 * @throws SecurityException if no permission to access workDir
	 * @throws IllegalArgumentException if workDir is not a valid directory
	 */
	public CRIUSupport setWorkDir(Path workDir) {
		Objects.requireNonNull(workDir, "Work directory cannot be null");
		if (!Files.isDirectory(workDir)) {
			throw new IllegalArgumentException("workDir is not a valid directory");
		}
		String dir = workDir.toAbsolutePath().toString();

		SecurityManager manager = System.getSecurityManager();
		if (manager != null) {
			manager.checkWrite(dir);
		}

		this.workDir = dir;
		return this;
	}

	/**
	 * Checkpoint the JVM. This operation will use the CRIU options set by the options setters.
	 *
	 * @throws UnsupportedOperationException if CRIU is not supported
	 * @throws JVMCheckpointException if a JVM error occurred before checkpoint
	 * @throws SystemCheckpointException if a CRIU operation failed
	 * @throws RestoreException if an error occurred during or after restore
	 */
	public void checkpointJVM() {
		if (isCRIUSupportEnabled()) {
			checkpointJVMImpl(imageDir,
					leaveRunning,
					shellJob,
					extUnixSupport,
					logLevel,
					logFile,
					fileLocks,
					workDir,
					tcpEstablished,
					autoDedup,
					trackMemory);
		} else {
			throw new UnsupportedOperationException("CRIU support is not enabled");
		}
	}
}
