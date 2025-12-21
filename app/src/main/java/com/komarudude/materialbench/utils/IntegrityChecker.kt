package com.komarudude.materialbench.utils

import android.content.Context
import android.content.pm.PackageManager
import android.util.Log
import com.komarudude.materialbench.BuildConfig

object IntegrityChecker {
    private const val COMPANION_PACKAGE = "com.komarudude.materialbench.rttest"

    fun isCompanionTrustworthy(context: Context): Boolean {
        return try {
            val pm = context.packageManager
            val expectedHash = BuildConfig.COMPANION_SHA256

            val packageInfo = pm.getPackageInfo(COMPANION_PACKAGE, PackageManager.GET_SIGNING_CERTIFICATES)
            val signingInfo = packageInfo.signingInfo ?: return false

            val signatures = if (signingInfo.hasMultipleSigners()) {
                signingInfo.apkContentsSigners
            } else {
                signingInfo.signingCertificateHistory
            }

            signatures?.any { sig ->
                getSha256(sig.toByteArray()) == expectedHash
            } ?: false
        } catch (e: Exception) {
            Log.e("Integrity", "Companion check failed: ${e.message}")
            false
        }
    }

    private fun getSha256(data: ByteArray): String {
        return java.security.MessageDigest.getInstance("SHA-256")
            .digest(data)
            .joinToString("") { "%02x".format(it) }
    }
}