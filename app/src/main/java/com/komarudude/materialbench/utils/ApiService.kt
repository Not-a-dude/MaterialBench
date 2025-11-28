package com.komarudude.materialbench.utils

import retrofit2.http.Body
import retrofit2.http.POST
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory

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
    private const val BASE_URL = "https://saver.privetbradok.ru/"

    val apiService: ApiService by lazy {
        Retrofit.Builder()
            .baseUrl(BASE_URL)
            .addConverterFactory(GsonConverterFactory.create())
            .build()
            .create(ApiService::class.java)
    }
}