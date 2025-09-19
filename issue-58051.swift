// https://github.com/swiftlang/swift/issues/58051

struct Date {}

struct URLSessionTaskTransactionMetrics {
  var responseEndDate: Date?
  var responseStartDate: Date?
  var requestEndDate: Date?
  var requestStartDate: Date?
  var connectEndDate: Date?
  var secureConnectionEndDate: Date?
  var secureConnectionStartDate: Date?
  var connectStartDate: Date?
  var domainLookupEndDate: Date?
  var domainLookupStartDate: Date?
  var fetchStartDate: Date?
}

fileprivate extension URLSessionTaskTransactionMetrics {
  var timeOfLastEvent: Date? {
    responseEndDate ?? responseStartDate ?? requestEndDate ?? requestStartDate ?? connectEndDate ?? secureConnectionEndDate ?? secureConnectionStartDate ?? connectStartDate ?? domainLookupEndDate ?? domainLookupStartDate ?? fetchStartDate
  }
}
