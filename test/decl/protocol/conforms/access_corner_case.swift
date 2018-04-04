// RUN: %target-swift-frontend -typecheck -verify %s

public protocol P {
  func publicRequirement()
}

protocol Q : P {
  func internalRequirement()
}

fileprivate protocol R : Q {
  func privateRequirement()
}

extension R {
  // The 'public'keyword here is a lie -- 'R' is private,
  // so any extension member is private also.

  public func publicRequirement() {}
  // expected-error@-1 {{method 'publicRequirement()' must be declared public because it matches a requirement in public protocol 'P'}}

  // Ditto for 'internal'. Note that the diagnostics are
  // misleading and crap.

  func internalRequirement() {}
  // expected-error@-1 {{method 'internalRequirement()' must be declared internal because it matches a requirement in internal protocol 'Q'}}

  // This is OK!
  public func privateRequirement() {}
}

// On the other hand, this pattern *is* allowed if the protocol
// is @usableFromInline, since there's no linking problem there.
//
// The standard library makes use of this pattern, unfortunately.
// The ArrayProtocol protocol is @usableFromInline internal, and
// it has public extension members that witness Sequence
// requirements.
public struct S : R {}

@usableFromInline
internal protocol U : P {}

extension U {
  public func publicRequirement() {}
}

public struct SS : U {}
