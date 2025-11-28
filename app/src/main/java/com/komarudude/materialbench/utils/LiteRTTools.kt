package com.komarudude.materialbench.utils

import android.content.Context
import com.google.ai.edge.litert.Accelerator
import com.google.ai.edge.litert.CompiledModel
import kotlin.math.exp

/**
 * MobileNetV4 wrapper.
 * - input: RGB ByteArray length = 384*384*3 (NHWC)
 * - output: ClassificationResult with top-K, logits and probs
 */
class MobileNetV4Classifier(
    private val context: Context,
    private val modelFileName: String = "mobilenetv4_conv_large.e600_r384_in1k_float16.tflite",
    private val labelFileName: String = "imagenet_classes.txt",
    private val accelerator: Accelerator = Accelerator.GPU
) {
    private val INPUT_HEIGHT = 384
    private val INPUT_WIDTH = 384
    private val INPUT_CHANNELS = 3
    private val INPUT_SIZE = INPUT_HEIGHT * INPUT_WIDTH * INPUT_CHANNELS

    private val MEAN = floatArrayOf(0.485f, 0.456f, 0.406f)
    private val STD = floatArrayOf(0.229f, 0.224f, 0.225f)

    private var compiledModel: CompiledModel? = null
    private var labels: List<String> = emptyList()

    data class TopKEntry(val classId: Int, val className: String, val prob: Float)

    data class ClassificationResult(
        val classId: Int,
        val confidence: Float,
        val className: String,
        val topK: List<TopKEntry> = emptyList(),
        val logits: FloatArray? = null,
        val probs: FloatArray? = null
    )

    fun initialize() {
        if (compiledModel != null) return
        try {
            labels = context.assets.open(labelFileName).bufferedReader().useLines { it.toList() }
            compiledModel = CompiledModel.create(
                context.assets,
                modelFileName,
                CompiledModel.Options(accelerator)
            )
        } catch (e: Exception) {
            e.printStackTrace()
            compiledModel = null
            labels = emptyList()
        }
    }

    private fun preprocessImage(imagePixels: ByteArray): FloatArray {
        require(imagePixels.size == INPUT_SIZE) { "Invalid input size. Expected $INPUT_SIZE bytes (RGB)." }

        val inputFloat = FloatArray(INPUT_SIZE)
        var dst = 0
        var src = 0
        while (src < imagePixels.size) {
            val r = (imagePixels[src++].toInt() and 0xFF) / 255.0f
            val g = (imagePixels[src++].toInt() and 0xFF) / 255.0f
            val b = (imagePixels[src++].toInt() and 0xFF) / 255.0f

            inputFloat[dst++] = (r - MEAN[0]) / STD[0]
            inputFloat[dst++] = (g - MEAN[1]) / STD[1]
            inputFloat[dst++] = (b - MEAN[2]) / STD[2]
        }
        return inputFloat
    }

    private fun softmax(logits: FloatArray): FloatArray {
        var max = Float.NEGATIVE_INFINITY
        for (v in logits) if (v > max) max = v

        var sum = 0.0f
        val exps = FloatArray(logits.size)
        for (i in logits.indices) {
            val e = exp(logits[i] - max)
            exps[i] = e
            sum += e
        }
        for (i in exps.indices) exps[i] = exps[i] / sum
        return exps
    }

    private fun topK(probs: FloatArray, k: Int = 5): List<TopKEntry> {
        val indices = probs.indices.sortedByDescending { probs[it] }.take(k)
        return indices.map { i ->
            TopKEntry(i, if (labels.size > i) labels[i] else "class_$i", probs[i])
        }
    }

    private fun argmax(probs: FloatArray): Int {
        var best = 0
        var bestVal = Float.NEGATIVE_INFINITY
        for (i in probs.indices) {
            if (probs[i] > bestVal) {
                bestVal = probs[i]
                best = i
            }
        }
        return best
    }

    /**
     * Run inference.
     * topKCount default = 5.
     */
    fun runInference(imagePixels: ByteArray, topKCount: Int = 5): ClassificationResult {
        val model = compiledModel ?: throw IllegalStateException("Model not initialized. Call initialize()")
        require(imagePixels.size == INPUT_SIZE) { "Invalid input size. Expected $INPUT_SIZE bytes (RGB)." }

        val inputFloat = preprocessImage(imagePixels)

        val inputBuffers = model.createInputBuffers()
        val outputBuffers = model.createOutputBuffers()

        try {
            inputBuffers[0].writeFloat(inputFloat)
            model.run(inputBuffers, outputBuffers)

            val outputFloat = outputBuffers[0].readFloat()
            val probs = softmax(outputFloat)

            val topIdx = argmax(probs)
            val confidence = probs.getOrElse(topIdx) { 0f }
            val name = if (labels.size > topIdx) labels[topIdx] else "class_$topIdx"
            val topKList = topK(probs, topKCount)

            return ClassificationResult(
                classId = topIdx,
                confidence = confidence,
                className = name,
                topK = topKList,
                logits = outputFloat,
                probs = probs
            )
        } finally {
            inputBuffers.forEach { it.close() }
            outputBuffers.forEach { it.close() }
        }
    }

    fun close() {
        compiledModel?.close()
        compiledModel = null
    }
}