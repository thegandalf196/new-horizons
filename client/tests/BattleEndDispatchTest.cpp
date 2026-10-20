/*
 * BattleEndDispatchTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../BattleEndDispatch.h"

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

struct Result
{
	int battleID = 17;
	int queryID = 23;
};

struct Receiver
{
	virtual ~Receiver() = default;
	virtual void battleEnd(int battleID, const Result * result, int queryID) = 0;
};

struct Interface final : Receiver
{
	std::function<void(int, const Result *, int)> callback;

	explicit Interface(std::function<void(int, const Result *, int)> callback)
		: callback(std::move(callback))
	{
	}

	void battleEnd(int battleID, const Result * result, int queryID) override
	{
		callback(battleID, result, queryID);
	}
};

struct Fixture
{
	std::map<int, std::shared_ptr<Interface>> primary;
	std::map<int, std::vector<std::shared_ptr<Receiver>>> additional;
	std::vector<int> calls;
	Result result;
	const std::thread::id owner = std::this_thread::get_id();

	std::shared_ptr<Interface> receiver(int identity, std::function<void()> effect = std::function<void()>{})
	{
		return std::make_shared<Interface>([this, identity, effect](int battleID, const Result * received, int queryID)
		{
			require(battleID == result.battleID && received == &result && queryID == result.queryID,
				"Terminal dispatch must preserve the exact battle/result/query tuple");
			require(std::this_thread::get_id() == owner, "Terminal callbacks must stay on their caller's thread");
			calls.push_back(identity);
			if(effect)
				effect();
		});
	}

	void dispatch(const std::vector<int> & players)
	{
		dispatchBattleEndToSnapshot(primary, additional, players, [this](Receiver & receiver)
		{
			receiver.battleEnd(result.battleID, &result, result.queryID);
		});
	}
};

void primaryUnregistersAdditional()
{
	Fixture fixture;
	std::weak_ptr<Receiver> allied;
	fixture.primary[0] = fixture.receiver(1, [&]()
	{
		fixture.additional.erase(0);
		require(!allied.expired(), "Unregistered allied AI must survive until terminal delivery");
	});
	fixture.additional[0].push_back(fixture.receiver(2));
	allied = fixture.additional.at(0).front();
	fixture.primary[1] = fixture.receiver(3);
	fixture.dispatch({0, 1});
	require(fixture.calls == std::vector<int>({1, 2, 3}), "Unregistered AI must receive battleEnd once after its primary");
	require(allied.expired(), "Snapshot must not retain an unregistered AI after dispatch returns");
}

void ordinaryOrderAndAbsentPlayers()
{
	Fixture fixture;
	fixture.primary[0] = fixture.receiver(1);
	fixture.additional[0].push_back(fixture.receiver(2));
	fixture.primary[1] = fixture.receiver(3);
	fixture.additional[1].push_back(fixture.receiver(4));
	fixture.primary[8] = fixture.receiver(5); // admitted spectator
	fixture.primary[9] = fixture.receiver(99); // unrelated player
	fixture.dispatch({0, 1, 8, 42});
	require(fixture.calls == std::vector<int>({1, 2, 3, 4, 5}), "Ordinary primary/additional/side/spectator order must remain");
	require(fixture.primary.size() == 4 && fixture.additional.size() == 2,
		"Absent recipients must not create registry entries");
}

void duplicateRegistrationDeliversOnce()
{
	Fixture fixture;
	auto receiver = fixture.receiver(1);
	fixture.primary[0] = receiver;
	fixture.additional[0] = {receiver, receiver};
	fixture.primary[1] = receiver;
	fixture.additional[1] = {receiver};
	fixture.dispatch({0, 1, 0});
	require(fixture.calls == std::vector<int>({1}), "One recipient must receive one terminal event despite repeated registration");
}

void replacementCannotReceiveOldResult()
{
	Fixture fixture;
	std::weak_ptr<Receiver> oldPrimary;
	std::weak_ptr<Receiver> oldDefender;
	fixture.primary[0] = fixture.receiver(1, [&]()
	{
		fixture.primary.clear();
		fixture.additional.clear();
		fixture.primary[1] = fixture.receiver(91);
		fixture.additional[0].push_back(fixture.receiver(92));
		require(!oldPrimary.expired() && !oldDefender.expired(), "Admitted receivers must survive registry replacement");
	});
	oldPrimary = fixture.primary.at(0);
	fixture.additional[0].push_back(fixture.receiver(2));
	fixture.primary[1] = fixture.receiver(3);
	oldDefender = fixture.primary.at(1);
	fixture.dispatch({0, 1});
	require(fixture.calls == std::vector<int>({1, 2, 3}), "Replacement receivers must not receive the previous battle's result");
	require(oldPrimary.expired() && oldDefender.expired(), "Old receivers must be released after terminal delivery");
}

void exceptionPolicyAndLifetime()
{
	Fixture fixture;
	std::weak_ptr<Receiver> allied;
	fixture.primary[0] = fixture.receiver(1, [&]()
	{
		fixture.additional.clear();
		throw std::runtime_error("primary failed");
	});
	fixture.additional[0].push_back(fixture.receiver(2));
	allied = fixture.additional.at(0).front();
	bool propagated = false;
	try
	{
		fixture.dispatch({0});
	}
	catch(const std::runtime_error & error)
	{
		propagated = std::string(error.what()) == "primary failed";
	}
	require(propagated && fixture.calls == std::vector<int>({1}), "Failure must propagate without retrying or continuing terminal delivery");
	require(allied.expired(), "Unwinding must release retained recipients");
}

void noRecipients()
{
	Fixture fixture;
	fixture.dispatch({0, 1});
	require(fixture.calls.empty() && fixture.primary.empty() && fixture.additional.empty(),
		"Empty registries must remain empty");
}
}

int main()
{
	try
	{
		primaryUnregistersAdditional();
		ordinaryOrderAndAbsentPlayers();
		duplicateRegistrationDeliversOnce();
		replacementCannotReceiveOldResult();
		exceptionPolicyAndLifetime();
		noRecipients();
		std::cout << "Battle end dispatch: all checks passed\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
