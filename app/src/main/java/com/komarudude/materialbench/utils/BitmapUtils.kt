package com.komarudude.materialbench.utils

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.content.Context
import java.io.InputStream
import java.nio.ByteBuffer

private const val EXPECTED_WIDTH = 384
private const val EXPECTED_HEIGHT = 384

/**
 * Loads a Bitmap from assets and converts it into an RGB byte array.
 * Scaling is skipped as the images are already prepared.
 * @param context The application context.
 * @param fullPath The full path to the file in assets (e.g., "human-faces/img.jpg").
 * @return An RGB byte array or null if an error occurs.
 */
fun loadAndPrepareImage(context: Context, fullPath: String): ByteArray? {
    val bitmap: Bitmap? = try {
        val inputStream: InputStream = context.assets.open(fullPath)
        BitmapFactory.decodeStream(inputStream)
    } catch (e: Exception) {
        println("Error loading Bitmap from $fullPath: ${e.message}")
        return null
    }

    if (bitmap == null) return null

    if (bitmap.width != EXPECTED_WIDTH || bitmap.height != EXPECTED_HEIGHT) {
        println("Size of $fullPath does not match: ${bitmap.width}x${bitmap.height}. Expected $EXPECTED_WIDTH x $EXPECTED_HEIGHT. Skipping.")
        bitmap.recycle()
        return null
    }

    val numPixels = EXPECTED_WIDTH * EXPECTED_HEIGHT
    val byteBuffer = ByteBuffer.allocate(numPixels * 4)
    bitmap.copyPixelsToBuffer(byteBuffer)
    byteBuffer.rewind()

    val rgbByteArray = ByteArray(numPixels * 3)
    var arrayIndex = 0
    while (byteBuffer.hasRemaining()) {
        byteBuffer.get()
        rgbByteArray[arrayIndex++] = byteBuffer.get() // R
        rgbByteArray[arrayIndex++] = byteBuffer.get() // G
        rgbByteArray[arrayIndex++] = byteBuffer.get() // B
    }

    bitmap.recycle()

    return rgbByteArray
}