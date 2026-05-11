// Wave 22 — GroupManager tests : create/add/remove/disband + transfer
// player entre groupes + leader promotion / transferring + capacity caps.
//
// Pattern aligne sur les autres tests : asserts + printf, pas de framework.
// Cible CTest : group_manager_tests.

#include "src/masterd/Groups/GroupManager.h"

#include <cassert>
#include <cstdio>

using namespace engine::server::groups;

namespace
{
	// ========================================================================
	// CreateGroup
	// ========================================================================

	void TestCreateGroupBasic()
	{
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(/*leader*/1, GroupType::Party);
		assert(id == 1);
		assert(mgr.GroupCount() == 1);
		// Le leader est dans le groupe.
		auto opt = mgr.GroupOfPlayer(1);
		assert(opt.has_value() && opt.value() == id);
		// Le groupe est de type Party, leader=1.
		const Group* g = mgr.Find(id);
		assert(g != nullptr);
		assert(g->Type() == GroupType::Party);
		assert(g->Leader() == 1);
		assert(g->MemberCount() == 1);
		std::puts("[OK] TestCreateGroupBasic");
	}

	void TestCreateGroupIdMonotonic()
	{
		GroupManager mgr;
		const GroupId a = mgr.CreateGroup(1, GroupType::Party);
		const GroupId b = mgr.CreateGroup(2, GroupType::Party);
		const GroupId c = mgr.CreateGroup(3, GroupType::Raid);
		assert(a < b && b < c);
		std::puts("[OK] TestCreateGroupIdMonotonic");
	}

	void TestCreateGroupTransferLeader()
	{
		// Si le leader est deja dans un autre groupe, il est retire de
		// l'ancien et place dans le nouveau.
		GroupManager mgr;
		const GroupId g1 = mgr.CreateGroup(1, GroupType::Party);
		mgr.AddMember(g1, 2);
		const GroupId g2 = mgr.CreateGroup(1, GroupType::Raid);
		// 1 est dans g2, pas dans g1.
		auto p1 = mgr.GroupOfPlayer(1);
		assert(p1.has_value() && p1.value() == g2);
		const Group* g1Ptr = mgr.Find(g1);
		assert(g1Ptr != nullptr);
		assert(!g1Ptr->HasMember(1));
		assert(g1Ptr->HasMember(2));  // 2 toujours dans g1
		std::puts("[OK] TestCreateGroupTransferLeader");
	}

	// ========================================================================
	// AddMember
	// ========================================================================

	void TestAddMember()
	{
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(1, GroupType::Party);
		assert(mgr.AddMember(id, 2));
		assert(mgr.AddMember(id, 3));
		const Group* g = mgr.Find(id);
		assert(g->MemberCount() == 3);
		assert(g->HasMember(1) && g->HasMember(2) && g->HasMember(3));
		// Idempotent : re-add d'un member retourne false.
		assert(!mgr.AddMember(id, 2));
		assert(g->MemberCount() == 3);
		std::puts("[OK] TestAddMember");
	}

	void TestAddMemberCapacityParty()
	{
		// Party cap = 5. Le leader compte comme 1 membre.
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(1, GroupType::Party);
		assert(mgr.AddMember(id, 2));
		assert(mgr.AddMember(id, 3));
		assert(mgr.AddMember(id, 4));
		assert(mgr.AddMember(id, 5));
		// 5 members, cap atteinte.
		assert(!mgr.AddMember(id, 6));
		assert(mgr.Find(id)->MemberCount() == 5);
		std::puts("[OK] TestAddMemberCapacityParty");
	}

	void TestAddMemberUnknownGroup()
	{
		GroupManager mgr;
		assert(!mgr.AddMember(/*inconnu*/999, 1));
		std::puts("[OK] TestAddMemberUnknownGroup");
	}

	void TestAddMemberTransferBetweenGroups()
	{
		// Si on ajoute un player deja dans un autre groupe, il est transfere.
		GroupManager mgr;
		const GroupId g1 = mgr.CreateGroup(1, GroupType::Party);
		const GroupId g2 = mgr.CreateGroup(10, GroupType::Party);
		mgr.AddMember(g1, 2);
		// Transfer 2 de g1 vers g2.
		assert(mgr.AddMember(g2, 2));
		assert(!mgr.Find(g1)->HasMember(2));
		assert(mgr.Find(g2)->HasMember(2));
		auto opt = mgr.GroupOfPlayer(2);
		assert(opt.has_value() && opt.value() == g2);
		std::puts("[OK] TestAddMemberTransferBetweenGroups");
	}

	// ========================================================================
	// RemoveMember + disband auto
	// ========================================================================

	void TestRemoveMember()
	{
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(1, GroupType::Party);
		mgr.AddMember(id, 2);
		mgr.AddMember(id, 3);
		mgr.RemoveMember(2);
		assert(!mgr.Find(id)->HasMember(2));
		assert(!mgr.GroupOfPlayer(2).has_value());
		// 1 et 3 toujours dans le groupe.
		assert(mgr.Find(id)->MemberCount() == 2);
		std::puts("[OK] TestRemoveMember");
	}

	void TestRemoveMemberDisbandWhenEmpty()
	{
		// Si on retire le dernier membre, le groupe est auto-supprime.
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(1, GroupType::Party);
		mgr.RemoveMember(1);
		assert(mgr.GroupCount() == 0);
		assert(mgr.Find(id) == nullptr);
		std::puts("[OK] TestRemoveMemberDisbandWhenEmpty");
	}

	void TestRemoveMemberLeaderTransfer()
	{
		// Si on retire le leader, le leadership passe a un autre membre.
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(1, GroupType::Party);
		mgr.AddMember(id, 2);
		mgr.AddMember(id, 3);
		mgr.RemoveMember(1);
		const Group* g = mgr.Find(id);
		assert(g != nullptr);
		assert(g->Leader() == 2 || g->Leader() == 3);  // ordre non-deterministe
		assert(g->MemberCount() == 2);
		std::puts("[OK] TestRemoveMemberLeaderTransfer");
	}

	// ========================================================================
	// Disband explicite
	// ========================================================================

	void TestDisband()
	{
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(1, GroupType::Party);
		mgr.AddMember(id, 2);
		mgr.AddMember(id, 3);
		mgr.Disband(id);
		assert(mgr.GroupCount() == 0);
		assert(!mgr.GroupOfPlayer(1).has_value());
		assert(!mgr.GroupOfPlayer(2).has_value());
		assert(!mgr.GroupOfPlayer(3).has_value());
		std::puts("[OK] TestDisband");
	}

	void TestDisbandUnknown()
	{
		GroupManager mgr;
		mgr.Disband(999);  // no-op
		assert(mgr.GroupCount() == 0);
		std::puts("[OK] TestDisbandUnknown");
	}

	// ========================================================================
	// Group methods directs (via Find)
	// ========================================================================

	void TestPromote()
	{
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(1, GroupType::Party);
		mgr.AddMember(id, 2);
		Group* g = mgr.Find(id);
		assert(g->Leader() == 1);
		assert(g->Promote(2));
		assert(g->Leader() == 2);
		// Promote sur non-membre : false.
		assert(!g->Promote(999));
		assert(g->Leader() == 2);
		std::puts("[OK] TestPromote");
	}

	void TestSetRole()
	{
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(1, GroupType::Party);
		mgr.AddMember(id, 2);
		Group* g = mgr.Find(id);
		assert(g->SetRole(2, GroupRole::Tank));
		// SetRole sur non-membre : false.
		assert(!g->SetRole(999, GroupRole::Heal));
		const auto& members = g->Members();
		auto it = members.find(2);
		assert(it != members.end() && it->second.role == GroupRole::Tank);
		std::puts("[OK] TestSetRole");
	}

	void TestSetLootMethod()
	{
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(1, GroupType::Party);
		Group* g = mgr.Find(id);
		assert(g->CurrentLootMethod() == LootMethod::FreeForAll);
		g->SetLootMethod(LootMethod::NeedBeforeGreed);
		assert(g->CurrentLootMethod() == LootMethod::NeedBeforeGreed);
		std::puts("[OK] TestSetLootMethod");
	}

	void TestRaidCapacity()
	{
		GroupManager mgr;
		const GroupId id = mgr.CreateGroup(1, GroupType::Raid);
		// Ajoute jusqu'a 40 (Raid cap). Leader compte = 1, donc 39 add.
		for (PlayerId p = 2; p <= 40; ++p)
		{
			bool ok = mgr.AddMember(id, p);
			assert(ok);
		}
		assert(mgr.Find(id)->MemberCount() == 40);
		// 41e : refuse.
		assert(!mgr.AddMember(id, 41));
		std::puts("[OK] TestRaidCapacity");
	}
}

int main()
{
	TestCreateGroupBasic();
	TestCreateGroupIdMonotonic();
	TestCreateGroupTransferLeader();
	TestAddMember();
	TestAddMemberCapacityParty();
	TestAddMemberUnknownGroup();
	TestAddMemberTransferBetweenGroups();
	TestRemoveMember();
	TestRemoveMemberDisbandWhenEmpty();
	TestRemoveMemberLeaderTransfer();
	TestDisband();
	TestDisbandUnknown();
	TestPromote();
	TestSetRole();
	TestSetLootMethod();
	TestRaidCapacity();
	std::puts("All GroupManager tests passed");
	return 0;
}
