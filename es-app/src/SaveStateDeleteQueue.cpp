#include "SaveStateDeleteQueue.h"

bool SaveStateDeleteQueue::enqueue(const std::string& stateFile, const std::string& screenshot)
{
	if (stateFile.empty())
		return false;
	if (mPending.count(stateFile))
		return false;

	mPending.insert(stateFile);
	mQueue.push_back(SaveStateDeleteJob{ stateFile, screenshot });
	return true;
}

bool SaveStateDeleteQueue::take(SaveStateDeleteJob& out)
{
	if (mRunning || mQueue.empty())
		return false;

	mCurrent = mQueue.front();
	mQueue.pop_front();
	mRunning = true;
	out = mCurrent;
	return true;
}

void SaveStateDeleteQueue::finish()
{
	if (!mRunning)
		return;

	mPending.erase(mCurrent.stateFile);
	mCurrent = SaveStateDeleteJob();
	mRunning = false;
	mCompleted++;
}

bool SaveStateDeleteQueue::isPending(const std::string& stateFile) const
{
	return !stateFile.empty() && mPending.count(stateFile) != 0;
}
