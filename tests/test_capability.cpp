// Contract 6 security tests: the capability grammar and the grant-
// subsumption truth table, pinned against src/script/capability.hpp.
// Everything here is constexpr, so the security semantics are compile-time
// facts — including the adversarial rows (label-boundary anchoring,
// homograph-class rejection, mid-label wildcards, fail-closed on malformed
// grants).

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "script/capability.hpp"

using script::capabilityGrantCovers;
using script::isValidCapabilityGrant;
using script::isValidCapabilityRequest;

TEST_CASE("capability grammar accepts the taxonomy and rejects malformed strings")
{
	// The initial vocabulary and representative scoped grants.
	STATIC_REQUIRE(isValidCapabilityGrant("storage"));
	STATIC_REQUIRE(isValidCapabilityGrant("voice"));
	STATIC_REQUIRE(isValidCapabilityGrant("insecure"));
	STATIC_REQUIRE(isValidCapabilityGrant("fs:read"));
	STATIC_REQUIRE(isValidCapabilityGrant("fs:read:worlds:save_1"));
	STATIC_REQUIRE(isValidCapabilityGrant("net:http:api.example.com"));
	STATIC_REQUIRE(isValidCapabilityGrant("net:http:*.example.com"));
	STATIC_REQUIRE(isValidCapabilityGrant("net:http:*"));
	STATIC_REQUIRE(isValidCapabilityGrant("net:connect:relay-1.example.com"));

	// Structural rejections.
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant(""));
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("fs::read"));     // empty segment
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant(":fs"));          // leading colon
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("fs:"));          // trailing colon
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("Fs:read"));      // uppercase
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("fs:read x"));    // space
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("fs:read/x"));    // '/' not in grammar
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("net:http:.example.com")); // leading dot
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("net:http:example.com.")); // trailing dot
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("net:http:a..b"));         // empty label

	// Wildcard shape rules: only "*" whole or leading "*." — and never in
	// the namespace segment.
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("net:http:ap*.example.com"));
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("net:http:*example.com"));
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("net:http:example.*"));
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("net:http:*."));
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("*"));
	STATIC_REQUIRE_FALSE(isValidCapabilityGrant("*:read"));

	// Requests are wildcard-free.
	STATIC_REQUIRE(isValidCapabilityRequest("net:http:api.example.com"));
	STATIC_REQUIRE_FALSE(isValidCapabilityRequest("net:http:*"));
	STATIC_REQUIRE_FALSE(isValidCapabilityRequest("net:http:*.example.com"));

	// Length and depth bounds (runtime: built strings).
	const std::string longSeg(64, 'a');
	REQUIRE_FALSE(isValidCapabilityGrant("fs:read:" + longSeg));
	REQUIRE(isValidCapabilityGrant("fs:read:" + std::string(63, 'a')));
	const std::string nineDeep = "a:b:c:d:e:f:g:h:i";
	REQUIRE_FALSE(isValidCapabilityGrant(nineDeep));
	REQUIRE(isValidCapabilityGrant("a:b:c:d:e:f:g:h"));
	const std::string tooLong = "fs:read:" + std::string(120, 'a') + ":" +
			std::string(120, 'a') + ":" + std::string(60, 'a');
	REQUIRE_FALSE(isValidCapabilityGrant(tooLong)); // > 255 chars total
}

TEST_CASE("grant subsumption: prefix semantics and exact matching")
{
	STATIC_REQUIRE(capabilityGrantCovers("fs:read", "fs:read"));
	STATIC_REQUIRE(capabilityGrantCovers("fs:read", "fs:read:worlds:save_1"));
	STATIC_REQUIRE(capabilityGrantCovers("fs", "fs:read:anything"));
	STATIC_REQUIRE(capabilityGrantCovers("storage", "storage"));

	STATIC_REQUIRE_FALSE(capabilityGrantCovers("fs:read", "fs:write:x"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("fs:read:x", "fs:read")); // grant narrower
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("storage", "voice"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("fs:read:worlds", "fs:read:worldsx"));
}

TEST_CASE("grant subsumption: host wildcards are label-boundary anchored")
{
	// What "*.example.com" MUST match...
	STATIC_REQUIRE(capabilityGrantCovers(
			"net:http:*.example.com", "net:http:api.example.com"));
	STATIC_REQUIRE(capabilityGrantCovers(
			"net:http:*.example.com", "net:http:a.b.example.com"));

	// ...and the attack rows it must NOT match.
	STATIC_REQUIRE_FALSE(capabilityGrantCovers( // bare apex: no extra label
			"net:http:*.example.com", "net:http:example.com"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers( // no dot boundary
			"net:http:*.example.com", "net:http:evilexample.com"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers( // suffix, not substring
			"net:http:*.example.com", "net:http:x.example.com.evil.net"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers( // hyphen confusion
			"net:http:*.example.com", "net:http:a-example.com"));

	// Bare "*" matches any single concrete segment, and prefix rules extend.
	STATIC_REQUIRE(capabilityGrantCovers("net:http:*", "net:http:anything.at-all.org"));
	STATIC_REQUIRE(capabilityGrantCovers("net:http:*", "net:http:host:8080.no")); // prefix
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("net:http:*", "net:connect:x"));

	// Short-suffix edge: "*.com" matches "a.com" but never "com"/"xcom".
	STATIC_REQUIRE(capabilityGrantCovers("net:http:*.com", "net:http:a.com"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("net:http:*.com", "net:http:com"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("net:http:*.com", "net:http:xcom"));
}

TEST_CASE("grant subsumption fails CLOSED on malformed input")
{
	// A malformed grant covers nothing (never fail open).
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("NET:http:x", "net:http:x"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("net::x", "net:http:x"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("", "net:http:x"));
	// A malformed or wildcarded request is granted nothing.
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("net:http:*", "net:http:*"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("net", "net:http:.bad.com"));
	STATIC_REQUIRE_FALSE(capabilityGrantCovers("fs:read", ""));
}
