# Optional: measure local LLM TTFT for README / R6 comparison (requires ollama + phi3).
# Example: ollama run phi3 --prompt "Say OK"  (manual); this script only documents the contract.
$ErrorActionPreference = "Stop"
Write-Host "baseline_phi3_latency.ps1 — placeholder."
Write-Host "Install Ollama, pull a small model (e.g. phi3), then measure time-to-first-token externally"
Write-Host "and record next to FP-SAN /status worst-tick and ShadowBrain veto microseconds in artefacts/R6_report.md."
