// https://github.com/swiftlang/swift/issues/53523

public func standardErrorOfEstimateForObservation(standardErrorOfEstimate: Double,
                                                  sampleSize: Int,
                                                  scoreOnThePredictor: Double,
                                                  meanOfThePredictor: Double,
                                                  standardDeviationOfThePredictor: Double) {
    return standardErrorOfEstimate *
        sqrt(1.0 +
            1.0 / sampleSize +
            square(scoreOnThePredictor - meanOfThePredictor) / (sampleSize * square(standardDeviationOfThePredictor)))
}

private func square(_ x: Double) -> Double {
    return x * x
}
