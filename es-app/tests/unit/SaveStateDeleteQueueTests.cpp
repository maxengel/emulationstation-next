// The save state manager's deletion queue, checked without a device.
//
// Everything here runs against es-app/src/SaveStateDeleteQueue.cpp alone --
// no thread, no file, no script. The worker around it (SaveStateDeleter)
// is the part the VM tests; these hold the rules the worker relies on
// (fork #205, D-UI-073): one job at a time, a pending file's tile is hidden,
// nothing is queued twice, and the finished count is how a page learns.

#include "doctest/doctest.h"

#include "SaveStateDeleteQueue.h"

#include <string>

TEST_CASE("a fresh queue is idle and has finished nothing")
{
	SaveStateDeleteQueue q;
	CHECK(q.idle());
	CHECK(q.queued() == 0);
	CHECK_FALSE(q.running());
	CHECK(q.completed() == 0);
	CHECK_FALSE(q.isPending("/storage/roms/savestates/nes/Bobl.state1"));

	SaveStateDeleteJob job;
	CHECK_FALSE(q.take(job));
	q.finish();   // nothing running: nothing changes
	CHECK(q.completed() == 0);
}

TEST_CASE("enqueue makes the file pending at once, before any job runs")
{
	SaveStateDeleteQueue q;
	CHECK(q.enqueue("/s/nes/Bobl.state4", "/s/nes/Bobl.state4.png"));
	CHECK(q.isPending("/s/nes/Bobl.state4"));
	CHECK(q.queued() == 1);
	CHECK_FALSE(q.idle());
	CHECK_FALSE(q.running());
	// The thumbnail is carried with the job; it is not a key of its own.
	CHECK_FALSE(q.isPending("/s/nes/Bobl.state4.png"));
}

TEST_CASE("a file is queued once while it is pending, and again after it has gone")
{
	SaveStateDeleteQueue q;
	CHECK(q.enqueue("/s/nes/Bobl.state4", ""));
	CHECK_FALSE(q.enqueue("/s/nes/Bobl.state4", ""));
	CHECK(q.queued() == 1);

	SaveStateDeleteJob job;
	REQUIRE(q.take(job));
	CHECK_FALSE(q.enqueue("/s/nes/Bobl.state4", ""));   // running counts as pending
	q.finish();
	CHECK_FALSE(q.isPending("/s/nes/Bobl.state4"));
	CHECK(q.enqueue("/s/nes/Bobl.state4", ""));         // gone, so it can go again
}

TEST_CASE("an empty state path is refused")
{
	SaveStateDeleteQueue q;
	CHECK_FALSE(q.enqueue("", "/s/nes/Bobl.state4.png"));
	CHECK(q.idle());
	CHECK_FALSE(q.isPending(""));
}

TEST_CASE("one job at a time: take hands out nothing while a job runs")
{
	SaveStateDeleteQueue q;
	q.enqueue("/s/nes/Bobl.state1", "/s/nes/Bobl.state1.png");
	q.enqueue("/s/nes/Bobl.state2", "");

	SaveStateDeleteJob first, second;
	REQUIRE(q.take(first));
	CHECK(first.stateFile == "/s/nes/Bobl.state1");
	CHECK(first.screenshot == "/s/nes/Bobl.state1.png");
	CHECK(q.running());
	CHECK(q.queued() == 1);

	CHECK_FALSE(q.take(second));   // the second waits for finish()
	CHECK(second.stateFile.empty());

	// Both stay pending: the first is running, the second is queued.
	CHECK(q.isPending("/s/nes/Bobl.state1"));
	CHECK(q.isPending("/s/nes/Bobl.state2"));

	q.finish();
	CHECK(q.completed() == 1);
	CHECK_FALSE(q.isPending("/s/nes/Bobl.state1"));
	CHECK(q.isPending("/s/nes/Bobl.state2"));

	REQUIRE(q.take(second));
	CHECK(second.stateFile == "/s/nes/Bobl.state2");
	CHECK(second.screenshot.empty());
	q.finish();
	CHECK(q.completed() == 2);
	CHECK(q.idle());
}

TEST_CASE("jobs run in the order they were asked for")
{
	SaveStateDeleteQueue q;
	const std::string a = "/s/gbc/Tobu.state3", b = "/s/gbc/Tobu.state1", c = "/s/gbc/Tobu.state.auto";
	q.enqueue(a, ""); q.enqueue(b, ""); q.enqueue(c, "");

	SaveStateDeleteJob job;
	REQUIRE(q.take(job)); CHECK(job.stateFile == a); q.finish();
	REQUIRE(q.take(job)); CHECK(job.stateFile == b); q.finish();
	REQUIRE(q.take(job)); CHECK(job.stateFile == c); q.finish();
	CHECK(q.completed() == 3);
	CHECK_FALSE(q.take(job));
}

TEST_CASE("completed moves once per finished job, which is what a page watches")
{
	SaveStateDeleteQueue q;
	unsigned seen = q.completed();
	q.enqueue("/s/nes/Bobl.state4", "");
	CHECK(q.completed() == seen);   // queued is not finished

	SaveStateDeleteJob job;
	q.take(job);
	CHECK(q.completed() == seen);   // running is not finished either
	q.finish();
	CHECK(q.completed() == seen + 1);
	q.finish();                     // a stray finish with nothing running
	CHECK(q.completed() == seen + 1);
}
