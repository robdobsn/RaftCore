"""Git sources of device type records (AssembleDeviceTypes.py), with local bare repositories

- A tag, a branch or a full commit hash; a folder of records or one record file in the repo; a short hash is an error
  with a hint.
- The lock file pins each source to the commit its ref resolved to (a moved tag doesn't change the build), and the
  records' hashes; --update resolves the refs again. A tampered cached record is an error.
- The cache: a locked commit already fetched needs no repository (an offline build); a locked commit not cached is
  fetched by hash, or - if the server doesn't allow that - by its ref, which must still be the locked commit.
- A local copy (<override-dir>/<source>) replaces a git source; a broken link is an error.
- raftdevtypes.json formatVersion and minRaftCore are checked; decode code outside the decode language in a record
  from a git source is an error; the same key from RaftCore and a git source must be named (M6).
"""

import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))
import AssembleDeviceTypes as A  # noqa: E402

GIT_ENV = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@t")


def git(*args, cwd=None):
    run = subprocess.run(["git"] + list(args), cwd=cwd, env=GIT_ENV, capture_output=True, text=True)
    if run.returncode != 0:
        raise RuntimeError(run.stderr)
    return run.stdout.strip()


def record(name, decode=None):
    rec = {"addresses": "0x50", "detectionValues": "0x00=0x12", "pollInfo": {"c": "0x00=r1", "i": 100, "s": 1},
           "devInfoJson": {"name": name, "resp": {"b": 1, "a": [{"n": "v", "t": "B", "o": "uint8"}]}}}
    if decode:
        rec["devInfoJson"]["resp"]["c"] = {"n": "custom", "c": decode}
    return rec


@unittest.skipUnless(shutil.which("git"), "git required")
class GitSourceTests(unittest.TestCase):

    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp())
        self.raftcore = self.tmp / "RaftCore"
        (self.raftcore / "devtypes" / "records").mkdir(parents=True)
        (self.raftcore / "devtypes" / "records" / "Alpha.json").write_text(json.dumps(record("Alpha")))
        (self.raftcore / "library.json").write_text(json.dumps({"version": "1.60.0"}))
        # A records repo: commit 1 (tag v1) has Gamma; commit 2 changes Gamma and adds Delta
        self.work = self.tmp / "work"
        (self.work / "records").mkdir(parents=True)
        git("init", "-q", "-b", "main", str(self.work))
        (self.work / "raftdevtypes.json").write_text(json.dumps({"name": "acme", "formatVersion": 1}))
        (self.work / "records" / "Gamma.json").write_text(json.dumps(record("Gamma v1")))
        (self.work / "README.md").write_text("not records")
        git("add", "-A", cwd=self.work)
        git("commit", "-q", "-m", "one", cwd=self.work)
        git("tag", "v1", cwd=self.work)
        self.commit1 = git("rev-parse", "HEAD", cwd=self.work)
        (self.work / "records" / "Gamma.json").write_text(json.dumps(record("Gamma v2")))
        (self.work / "records" / "Delta.json").write_text(json.dumps(record("Delta")))
        git("add", "-A", cwd=self.work)
        git("commit", "-q", "-m", "two", cwd=self.work)
        self.commit2 = git("rev-parse", "HEAD", cwd=self.work)
        self.bare = self.tmp / "acme.git"
        git("clone", "-q", "--bare", str(self.work), str(self.bare))
        git("config", "uploadpack.allowAnySHA1InWant", "true", cwd=self.bare)
        git("config", "uploadpack.allowFilter", "true", cwd=self.bare)
        self.url = "file://" + str(self.bare)
        self.project = self.tmp / "project" / "systypes" / "Main"
        self.project.mkdir(parents=True)
        self.cache = self.tmp / "cache"
        self.lock = self.project / "DeviceTypeSources.lock"
        self.override = self.tmp / "project" / "raftdevtypes"

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def select(self, ref="v1", path=None, include=None, extra_sources=None):
        spec = {"git": self.url, "ref": ref}
        if path:
            spec["path"] = path
        sources = {"acme": spec}
        sources.update(extra_sources or {})
        content = {"sources": sources}
        if include is not None:
            content["builtin"] = {"include": include}
        sel = self.project / "DeviceTypeSources.json"
        sel.write_text(json.dumps(content))
        return str(sel)

    def run_assemble(self, sel, update=False):
        options = A.GitOptions(str(self.cache), str(self.lock), str(self.override), update)
        result = A.assemble([sel], str(self.raftcore), "builtin", options)
        if result.lock_text is not None:
            A.write_if_changed(str(self.lock), result.lock_text)
        return result

    def name_of(self, result, key):
        return result.records[key]["devInfoJson"]["name"]

    def assertAssembleError(self, text, sel, update=False):
        with self.assertRaises(A.AssembleError) as ctx:
            self.run_assemble(sel, update)
        self.assertIn(text, str(ctx.exception))

    def test_tag_pinned_by_the_lock(self):
        sel = self.select("v1")
        result = self.run_assemble(sel)
        self.assertEqual(list(result.records), ["Gamma"])
        self.assertEqual(self.name_of(result, "Gamma"), "Gamma v1")
        self.assertTrue(any("lock updated" in m for m in result.messages))
        lock = json.loads(self.lock.read_text())
        self.assertEqual(lock["sources"]["acme"], {"git": self.url, "ref": "v1", "commit": self.commit1})
        self.assertEqual(lock["records"]["Gamma"]["commit"], self.commit1)
        self.assertEqual(len(lock["records"]["Gamma"]["sha256"]), 64)
        manifest = json.loads(A.manifest_text(result, "builtin"))
        self.assertEqual(manifest["records"]["Gamma"]["commit"], self.commit1)

        # The tag moves: the lock keeps the build on the locked commit, even with nothing cached
        git("tag", "-f", "v1", self.commit2, cwd=self.bare)
        self.assertEqual(self.name_of(self.run_assemble(sel), "Gamma"), "Gamma v1")
        shutil.rmtree(self.cache)
        result = self.run_assemble(sel)
        self.assertEqual(self.name_of(result, "Gamma"), "Gamma v1")
        self.assertEqual(result.lock_text, None, "lock unchanged")

        # --update resolves the ref again
        result = self.run_assemble(sel, update=True)
        self.assertEqual(self.name_of(result, "Gamma"), "Gamma v2")
        self.assertEqual(json.loads(self.lock.read_text())["sources"]["acme"]["commit"], self.commit2)

    def test_offline_from_the_cache(self):
        sel = self.select("v1")
        self.run_assemble(sel)
        shutil.rmtree(self.bare)
        self.assertEqual(self.name_of(self.run_assemble(sel), "Gamma"), "Gamma v1")

    def test_branch_commit_and_file(self):
        self.assertEqual(list(self.run_assemble(self.select("main")).records), ["Delta", "Gamma"])
        self.lock.unlink()
        result = self.run_assemble(self.select(self.commit1))
        self.assertEqual(self.name_of(result, "Gamma"), "Gamma v1")
        self.lock.unlink()
        result = self.run_assemble(self.select("main", path="records/Delta.json"))
        self.assertEqual(list(result.records), ["Delta"])
        self.lock.unlink()
        self.assertAssembleError("full 40-character hash", self.select(self.commit1[:10]))
        self.assertAssembleError("can't fetch", self.select("v9"))
        self.assertAssembleError("not found in the repo", self.select("v1", path="nothing"))

    def test_server_without_fetch_by_hash(self):
        # Protocol v0, without allowAnySHA1InWant: only an advertised commit (a ref's tip) can be fetched by hash
        git("config", "uploadpack.allowAnySHA1InWant", "false", cwd=self.bare)
        saved = {k: os.environ.get(k) for k in ("GIT_CONFIG_COUNT", "GIT_CONFIG_KEY_0", "GIT_CONFIG_VALUE_0")}
        os.environ.update({"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "protocol.version", "GIT_CONFIG_VALUE_0": "0"})
        self.addCleanup(lambda: [os.environ.pop(k, None) if v is None else os.environ.__setitem__(k, v)
                                 for k, v in saved.items()])
        sel = self.select("v1")
        self.run_assemble(sel)
        shutil.rmtree(self.cache)
        # Not cached: fetched again (the locked commit is still v1's tip)
        self.assertEqual(self.name_of(self.run_assemble(sel), "Gamma"), "Gamma v1")
        # The ref has moved, so the locked commit isn't advertised and is fetched by the ref: an error that says how
        # to take the new commit
        shutil.rmtree(self.cache)
        git("tag", "-f", "v1", self.commit2, cwd=self.bare)
        self.assertAssembleError("update the lock", sel)

    def test_tampered_cache(self):
        sel = self.select("v1")
        self.run_assemble(sel)
        cached = next(self.cache.rglob("Gamma.json"))
        cached.write_text(json.dumps(record("Gamma changed")))
        self.assertAssembleError("differs from the lock", sel)

    def test_local_override(self):
        sel = self.select("v1")
        self.run_assemble(sel)
        (self.override / "acme" / "records").mkdir(parents=True)
        (self.override / "acme" / "records" / "Gamma.json").write_text(json.dumps(record("Gamma local")))
        result = self.run_assemble(sel)
        self.assertEqual(self.name_of(result, "Gamma"), "Gamma local")
        self.assertTrue(any("local copy" in m for m in result.messages))
        self.assertIn("acme", json.loads(self.lock.read_text())["sources"], "the lock keeps the source")
        shutil.rmtree(self.override / "acme")
        os.symlink(str(self.tmp / "nowhere"), str(self.override / "acme"))
        self.assertAssembleError("broken link", sel)

    def test_source_manifest(self):
        (self.work / "raftdevtypes.json").write_text(json.dumps({"formatVersion": 1, "minRaftCore": "9.0.0"}))
        git("commit", "-q", "-am", "needs newer", cwd=self.work)
        git("push", "-q", str(self.bare), "main", cwd=self.work)
        self.assertAssembleError("needs RaftCore 9.0.0 or later", self.select("main"))
        (self.work / "raftdevtypes.json").write_text(json.dumps({"formatVersion": 2}))
        git("commit", "-q", "-am", "format 2", cwd=self.work)
        git("push", "-q", str(self.bare), "main", cwd=self.work)
        self.assertAssembleError("formatVersion 2", self.select("main"), update=True)

    def test_decode_code_from_git_is_checked(self):
        (self.work / "records" / "Evil.json").write_text(json.dumps(record("Evil", "out.v=buf[0];system(1);")))
        git("add", "-A", cwd=self.work)
        git("commit", "-q", "-m", "evil", cwd=self.work)
        git("push", "-q", str(self.bare), "main", cwd=self.work)
        self.assertAssembleError("decode code (resp.c) isn't allowed", self.select("main"))
        (self.work / "records" / "Evil.json").write_text(json.dumps(record("Evil", "int k=0;out.v=buf[k];")))
        git("commit", "-q", "-am", "fine", cwd=self.work)
        git("push", "-q", str(self.bare), "main", cwd=self.work)
        self.assertIn("Evil", self.run_assemble(self.select("main"), update=True).records)

    def test_same_key_from_raftcore_and_git(self):
        (self.work / "records" / "Alpha.json").write_text(json.dumps(record("Alpha acme")))
        git("add", "-A", cwd=self.work)
        git("commit", "-q", "-m", "alpha", cwd=self.work)
        git("push", "-q", str(self.bare), "main", cwd=self.work)
        extra = {"raftcore": {"bundled": True}}
        self.assertAssembleError("name the source to use", self.select("main", extra_sources=extra))
        result = self.run_assemble(self.select("main", include=["acme:Alpha", "raftcore:*"], extra_sources=extra),
                                   update=True)
        self.assertEqual(self.name_of(result, "Alpha"), "Alpha acme")

    def test_no_git_sources_no_lock(self):
        sel = self.project / "DeviceTypeSources.json"
        sel.write_text(json.dumps({"sources": {"raftcore": {"bundled": True}}}))
        self.run_assemble(str(sel))
        self.assertFalse(self.lock.exists())


if __name__ == "__main__":
    unittest.main()
