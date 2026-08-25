# Security Policy

## Supported code

TPPL is currently an alpha-stage project without stable release tags. Security
fixes are made against the latest code on the default `main` branch.

Older commits, development branches, and unpublished snapshots do not receive
guaranteed backports. If a future release policy changes this, the supported
versions will be documented here.

## Reporting a vulnerability

Please do not open a public issue for a suspected vulnerability or publish
exploit details before disclosure has been coordinated.

Report the issue privately using either:

- [GitHub Private Vulnerability Reporting](https://github.com/koljaPl/pseudo-programming-language/security/advisories/new);
- email at [nikolya.plugin@gmail.com](mailto:nikolya.plugin@gmail.com).

Include as much of the following as is practical:

- the affected commit or branch;
- operating system, compiler, and relevant tool versions;
- a minimal reproducer or proof of concept;
- the expected and observed behavior;
- the security impact and realistic attack scenario;
- any mitigation or proposed fix you have identified.

Reports are handled on a best-effort basis. The maintainer will acknowledge a
report and provide updates as soon as practical, but this solo-maintained alpha
project does not promise a fixed response or remediation SLA.

## Security scope

Examples of issues that belong in a private security report include:

- crashes, hangs, or undefined behavior caused by malformed TPPL input when
  they create a realistic security impact;
- unsafe process execution, argument handling, or process cleanup in the
  toolchain layer;
- insecure temporary-file or compiled-artifact handling;
- unintended file-descriptor, filesystem, or command-execution exposure;
- runtime bounds or input-handling defects with security consequences;
- incorrect generated C++ that creates a security issue for otherwise valid
  TPPL source.

The following are outside TPPL's security boundary:

- executing an untrusted generated binary;
- using TPPL or `GppCompiler` as a sandbox or isolation boundary;
- intentionally resource-exhausting programs in an unrestricted environment;
- unsupported language features or ordinary correctness bugs without a
  security impact;
- vulnerabilities in GCC, Clang, the operating system, or other upstream
  dependencies.

Generated programs are native executables and can perform anything permitted
by their host process. Compile and run untrusted input only inside an isolation
environment designed for that purpose.

Ordinary bugs and feature requests should use the public
[GitHub issue tracker](https://github.com/koljaPl/pseudo-programming-language/issues).
Upstream vulnerabilities should be reported to the affected upstream project.

## Coordinated disclosure

Please allow time to investigate and prepare a fix before public disclosure.
For a confirmed vulnerability, the maintainer will coordinate the fix and the
publication of a GitHub Security Advisory with the reporter where practical.
Confidential report details will not be published before an agreed disclosure
or before a fix is ready, unless disclosure is required to protect users from
an active threat.
