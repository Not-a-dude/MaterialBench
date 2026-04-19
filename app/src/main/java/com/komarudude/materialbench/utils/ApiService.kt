package com.komarudude.materialbench.utils

import com.komarudude.materialbench.BuildConfig
import okhttp3.OkHttpClient
import okhttp3.logging.HttpLoggingInterceptor

import retrofit2.http.Body
import retrofit2.http.POST
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import java.util.concurrent.TimeUnit

data class ScoreRequest(
    val score: Int,
    val versionCode: Long
)

data class SubmitResponse(
    val message: Boolean
)

data class RankResponse(
    val percentile: Double
)

interface ApiService {
    @POST("getRank")
    suspend fun getRank(@Body req: ScoreRequest): RankResponse

    @POST("submit")
    suspend fun submit(@Body req: ScoreRequest): SubmitResponse
}

object RetrofitClient {
    private const val BASE_URL = "https://dude.privetbradok.space:9560/"

    private val okHttpClient: OkHttpClient by lazy {
        val builder = OkHttpClient.Builder()
            .connectTimeout(10, TimeUnit.SECONDS)
            .readTimeout(10, TimeUnit.SECONDS)

        if (BuildConfig.DEBUG) {
            val logging = HttpLoggingInterceptor().apply {
                level = HttpLoggingInterceptor.Level.BODY
            }
            builder.addInterceptor(logging)
        }

        builder.build()
    }

    val apiService: ApiService by lazy {
        Retrofit.Builder()
            .baseUrl(BASE_URL)
            .client(okHttpClient)
            .addConverterFactory(GsonConverterFactory.create())
            .build()
            .create(ApiService::class.java)
    }
}