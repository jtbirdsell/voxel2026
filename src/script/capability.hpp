// Contract 6 reference codec — the compiled anchor for the capability-
// taxonomy freeze (docs/contracts/contract-6-capabilities.md). Header-only.
//
// The security-critical operation this header exists to pin is SUBSUMPTION:
// "does grant G cover request R". Everything here is written for that check
// to be boring, total, and attack-tested — the test file carries the
// adversarial table (label-boundary anchoring, homograph rejection,
// mid-label wildcards, empty segments).
//
// Frozen grammar (ASCII only, by design — no IDN homographs in v1):
//   capability := segment (':' segment)*
//   segment    := 1..63 chars of [a-z0-9_.*-], no leading/trailing '.',
//                 no empty dot-labels ("a..b" invalid)
//   whole string <= 255 chars, <= 8 segments
//   '*' may appear ONLY as (a) an entire segment, or (b) the leading
//   dot-label of a segment ("*.example.com"); never mid-label ("ap*.x"
//   invalid), never in the first segment (the namespace is always literal).
//
// Frozen matching semantics (deny-by-default sits ABOVE this: no grant, no
// access; grants are additive; there are no negative grants in v1):
//   A grant matches a request iff the grant's segments align with a PREFIX
//   of the request's segments (a shorter grant is a broader grant:
//   "fs:read" covers "fs:read:anything"), where each grant segment must
//   match its request segment as:
//     literal      -> byte equality (both sides are validated lowercase)
//     "*"          -> any single segment
//     "*.suffix"   -> request segment must be "<one-or-more-labels>.suffix"
//                     — anchored at a DOT boundary, so "*.example.com"
//                     matches "api.example.com" and "a.b.example.com" but
//                     NOT "example.com" (no extra label), NOT
//                     "evilexample.com" (no dot boundary), NOT a request
//                     of "x.example.com.evil.net" (suffix, not substring).
//   Requests must be wildcard-free (a request names a concrete thing).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace script {

inline constexpr std::size_t kCapabilityMaxLength = 255;
inline constexpr std::size_t kCapabilityMaxSegments = 8;
inline constexpr std::size_t kCapabilitySegmentMaxLength = 63;

namespace detail {

constexpr bool capabilityChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
			c == '*' || c == '-';
}

// One grammar pass over a segment: charset, length, dot-label structure,
// and the two legal wildcard shapes.
constexpr bool validSegment(std::string_view seg, bool wildcardAllowed)
{
	if (seg.empty() || seg.size() > kCapabilitySegmentMaxLength)
		return false;
	if (seg.front() == '.' || seg.back() == '.')
		return false;
	bool prevDot = false;
	for (std::size_t i = 0; i < seg.size(); ++i) {
		const char c = seg[i];
		if (!capabilityChar(c))
			return false;
		if (c == '.') {
			if (prevDot)
				return false; // empty dot-label
			prevDot = true;
			continue;
		}
		prevDot = false;
		if (c == '*') {
			if (!wildcardAllowed)
				return false;
			// Legal shapes only: "*" alone, or leading "*." label.
			if (i != 0)
				return false;
			if (seg.size() != 1 && seg[1] != '.')
				return false;
		}
	}
	return true;
}

struct SegmentRange {
	std::size_t begin = 0, end = 0;
};

// Split (validating): fills `out` and returns the count, or 0 on any
// grammar violation. `wildcardOk` excludes segment 0 always (namespaces
// are literal).
constexpr std::size_t splitValidate(std::string_view cap,
		SegmentRange (&out)[kCapabilityMaxSegments], bool wildcardOk)
{
	if (cap.empty() || cap.size() > kCapabilityMaxLength)
		return 0;
	std::size_t count = 0;
	std::size_t begin = 0;
	for (std::size_t i = 0; i <= cap.size(); ++i) {
		if (i != cap.size() && cap[i] != ':')
			continue;
		if (count == kCapabilityMaxSegments)
			return 0;
		const std::string_view seg = cap.substr(begin, i - begin);
		if (!validSegment(seg, wildcardOk && count != 0))
			return 0;
		out[count++] = SegmentRange{begin, i};
		begin = i + 1;
	}
	return count;
}

// "*.suffix" against a concrete segment: the request must end with
// ".suffix" AND have at least one extra leading label. Anchoring on the
// '.' boundary is the defense against "evilexample.com".
constexpr bool wildcardLabelMatch(std::string_view pattern, std::string_view request)
{
	const std::string_view suffix = pattern.substr(1); // ".example.com"
	if (request.size() < suffix.size() + 1)            // >= 1 char of extra label
		return false;
	if (request.substr(request.size() - suffix.size()) != suffix)
		return false;
	return true;
}

} // namespace detail

// Grammar checks. Grants may carry wildcards (outside the namespace
// segment); requests never may.
constexpr bool isValidCapabilityGrant(std::string_view grant)
{
	detail::SegmentRange segs[kCapabilityMaxSegments];
	return detail::splitValidate(grant, segs, /*wildcardOk=*/true) != 0;
}

constexpr bool isValidCapabilityRequest(std::string_view request)
{
	detail::SegmentRange segs[kCapabilityMaxSegments];
	return detail::splitValidate(request, segs, /*wildcardOk=*/false) != 0;
}

// THE check: does `grant` cover `request`? Returns false — never throws,
// never guesses — if either side fails the grammar (a malformed grant must
// fail CLOSED, not open).
constexpr bool capabilityGrantCovers(std::string_view grant, std::string_view request)
{
	detail::SegmentRange gs[kCapabilityMaxSegments];
	detail::SegmentRange rs[kCapabilityMaxSegments];
	const std::size_t gn = detail::splitValidate(grant, gs, /*wildcardOk=*/true);
	const std::size_t rn = detail::splitValidate(request, rs, /*wildcardOk=*/false);
	if (gn == 0 || rn == 0 || gn > rn)
		return false; // malformed, or grant more specific than request
	for (std::size_t i = 0; i < gn; ++i) {
		const std::string_view g = grant.substr(gs[i].begin, gs[i].end - gs[i].begin);
		const std::string_view r =
				request.substr(rs[i].begin, rs[i].end - rs[i].begin);
		if (g == "*")
			continue;
		if (g.size() >= 2 && g[0] == '*' && g[1] == '.') {
			if (!detail::wildcardLabelMatch(g, r))
				return false;
			continue;
		}
		if (g != r)
			return false;
	}
	return true;
}

} // namespace script
