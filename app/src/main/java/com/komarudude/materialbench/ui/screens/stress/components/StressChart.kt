package com.komarudude.materialbench.ui.screens.stress.components

import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.patrykandpatrick.vico.compose.cartesian.CartesianChartHost
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberBottom
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberEnd
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberStart
import com.patrykandpatrick.vico.compose.cartesian.layer.rememberLine
import com.patrykandpatrick.vico.compose.cartesian.layer.rememberLineCartesianLayer
import com.patrykandpatrick.vico.compose.cartesian.marker.rememberDefaultCartesianMarker
import com.patrykandpatrick.vico.compose.cartesian.rememberCartesianChart
import com.patrykandpatrick.vico.compose.cartesian.rememberVicoZoomState
import com.patrykandpatrick.vico.compose.common.component.rememberShapeComponent
import com.patrykandpatrick.vico.compose.common.component.rememberTextComponent
import com.patrykandpatrick.vico.core.cartesian.Zoom
import com.patrykandpatrick.vico.core.cartesian.axis.Axis
import com.patrykandpatrick.vico.core.cartesian.axis.HorizontalAxis
import com.patrykandpatrick.vico.core.cartesian.axis.VerticalAxis
import com.patrykandpatrick.vico.core.cartesian.data.CartesianChartModelProducer
import com.patrykandpatrick.vico.core.cartesian.data.CartesianLayerRangeProvider
import com.patrykandpatrick.vico.core.cartesian.data.CartesianValueFormatter
import com.patrykandpatrick.vico.core.cartesian.layer.LineCartesianLayer
import com.patrykandpatrick.vico.core.cartesian.marker.DefaultCartesianMarker
import com.patrykandpatrick.vico.core.common.Fill
import com.patrykandpatrick.vico.core.common.data.ExtraStore
import java.text.DecimalFormat

private val TempAxisValueFormatter = CartesianValueFormatter.decimal(DecimalFormat("#.## °C"))
private val PerfAxisValueFormatter = CartesianValueFormatter.decimal(DecimalFormat("# Pts"))

private val MarkerValueFormatter =
    DefaultCartesianMarker.ValueFormatter.default(DecimalFormat("#.##"))

@Composable
fun StressChart(
    modelProducer: CartesianChartModelProducer,
    bottomAxisValueFormatter: CartesianValueFormatter,
    modifier: Modifier = Modifier,
) {
    val labelBackground = rememberShapeComponent(
        fill = Fill(MaterialTheme.colorScheme.surface.toArgb())
    )

    val labelComponent = rememberTextComponent(
        color = Color.White,
        textSize = 12.sp,
        background = labelBackground
    )

    val zoomState = rememberVicoZoomState(initialZoom = Zoom.Content)

    val tempRangeProvider = remember {
        object : CartesianLayerRangeProvider {
            override fun getMinY(minY: Double, maxY: Double, extraStore: ExtraStore): Double = 18.0
            override fun getMaxY(minY: Double, maxY: Double, extraStore: ExtraStore): Double = 46.0
        }
    }

    CartesianChartHost(
        chart = rememberCartesianChart(
            rememberLineCartesianLayer(
                verticalAxisPosition = Axis.Position.Vertical.Start,
                lineProvider = LineCartesianLayer.LineProvider.series(
                    LineCartesianLayer.rememberLine(fill = LineCartesianLayer.LineFill.single(Fill(
                        MaterialTheme.colorScheme.error.toArgb()
                    )))
                ),
                rangeProvider = tempRangeProvider
            ),
            rememberLineCartesianLayer(
                verticalAxisPosition = Axis.Position.Vertical.End,
                lineProvider = LineCartesianLayer.LineProvider.series(
                    LineCartesianLayer.rememberLine(fill = LineCartesianLayer.LineFill.single(Fill(
                        MaterialTheme.colorScheme.primary.toArgb()))),
                    LineCartesianLayer.rememberLine(fill = LineCartesianLayer.LineFill.single(Fill(MaterialTheme.colorScheme.tertiary.toArgb())))
                )
            ),
            startAxis = VerticalAxis.rememberStart(valueFormatter = TempAxisValueFormatter),
            endAxis = VerticalAxis.rememberEnd(valueFormatter = PerfAxisValueFormatter),
            bottomAxis = HorizontalAxis.rememberBottom(
                guideline = null,
                valueFormatter = bottomAxisValueFormatter
            ),
            marker = rememberDefaultCartesianMarker(
                label = labelComponent,
                valueFormatter = MarkerValueFormatter
            ),
        ),
        modelProducer = modelProducer,
        zoomState = zoomState,
        modifier = modifier.height(220.dp),
    )
}