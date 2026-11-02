"""Regressions for the post-CI fuzzer's scheduling contract."""
import unittest
from pathlib import Path

from check_workflows import fuzz_gate_problems


def workflow(condition=None, needs="passed"):
    guard = f"    if: {condition}\n" if condition is not None else ""
    return ("jobs:\n  fuzz:\n    name: Fuzz\n"
            f"    needs: {needs}\n{guard}"
            "    runs-on: [self-hosted, indigo-build]\n"
            "    steps:\n      - run: fuzz\n"
            "  target:\n    if: github.event_name == 'pull_request'\n")


class FuzzGateTests(unittest.TestCase):
    def test_old_implicit_success_gate_is_rejected(self):
        # CI passed can succeed after Target branch is skipped. An implicit
        # success() still skips its descendant Fuzz in a workflow dispatch.
        self.assertTrue(fuzz_gate_problems(workflow()))

    def test_aggregate_success_alone_keeps_implicit_status_gate(self):
        self.assertTrue(fuzz_gate_problems(workflow("needs.passed.result == 'success'")))

    def test_always_alone_would_fuzz_after_failure_or_cancellation(self):
        self.assertTrue(fuzz_gate_problems(workflow("always()")))

    def test_cancellation_guard_alone_would_fuzz_after_failed_ci(self):
        self.assertTrue(fuzz_gate_problems(workflow("${{ !cancelled() }}")))

    def test_success_gate_without_cancellation_is_rejected(self):
        self.assertTrue(fuzz_gate_problems(workflow(
            "always() && needs.passed.result == 'success'")))

    def test_failure_or_skip_cannot_replace_success(self):
        for result in ("failure", "skipped", "cancelled"):
            with self.subTest(result=result):
                self.assertTrue(fuzz_gate_problems(workflow(
                    "${{ !cancelled() && needs.passed.result == '" + result + "' }}")))

    def test_guard_accepts_explicit_status_check_and_success(self):
        self.assertEqual(fuzz_gate_problems(workflow(
            "${{ !cancelled() && needs.passed.result == 'success' }}")), [])

    def test_optional_always_with_both_required_guards(self):
        self.assertEqual(fuzz_gate_problems(workflow(
            "always() && !cancelled() && needs.passed.result == 'success'")), [])

    def test_changing_dependency_cannot_bypass_aggregate(self):
        self.assertTrue(fuzz_gate_problems(workflow(
            "${{ !cancelled() && needs.passed.result == 'success' }}", needs="build")))

    def test_missing_job_is_rejected(self):
        self.assertTrue(fuzz_gate_problems("jobs:\n  passed:\n    if: always()\n"))

    def test_real_workflow_and_missing_guard_mutant(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / ".github/workflows/ci.yml").read_text()
        self.assertEqual(fuzz_gate_problems(source), [])
        guard = "    if: ${{ !cancelled() && needs.passed.result == 'success' }}\n"
        self.assertEqual(source.count(guard), 1)
        self.assertTrue(fuzz_gate_problems(source.replace(guard, "", 1)))


if __name__ == "__main__":
    unittest.main()
