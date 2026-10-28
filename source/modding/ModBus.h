/* ModBus.h
Copyright (c) 2026 by Anaxarchus

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <set>
#include <string>

class ModLoaders;



// The notification bus between mods (mod API 10's notify, listen, unlisten
// and EsLoader::notification): mods under any loader post named notifications
// with an opaque text payload, and the game delivers them to the mods that
// listen. Posts are queued, never delivered from inside the post; the game
// drains the queue at fixed points (see ModHarness), and each delivery goes to
// the receiver's loader, which brackets it as the receiver's hook. It knows
// nothing of the game's world. Main thread only.
class ModBus {
public:
	static constexpr size_t MAX_QUEUED = 1024;
	static constexpr int MAX_POSTS_PER_DRAIN = 256;
	static constexpr int MAX_ROUNDS = 4;
	static constexpr size_t MAX_DELIVERIES = 1024;
	static constexpr size_t MAX_NAME = 128;
	static constexpr size_t MAX_PAYLOAD = 64 * 1024;
	// The most bytes (names and payloads) waiting at once, in the queue and in
	// the part of it a drain holds.
	static constexpr size_t MAX_QUEUED_BYTES = 4 * 1024 * 1024;
	static constexpr size_t MAX_PATTERNS = 256;


public:
	explicit ModBus(ModLoaders &loaders);

	// A notification name: 1 to MAX_NAME bytes of A-Z a-z 0-9 . _ : / -. A
	// pattern may also end in '*' (and may be "*" alone).
	static bool ValidName(const char *name, bool pattern);

	// Queue a notification from `sender` (the game's record of the running mod,
	// never the loader's word). Answers whether it was queued: it is refused for
	// a bad name or a payload over MAX_PAYLOAD, and dropped when MAX_QUEUED
	// notifications or MAX_QUEUED_BYTES wait already (counting those a drain
	// holds back), or the sender has posted MAX_POSTS_PER_DRAIN since the last drain.
	// Refusals are logged, at most 20 a minute.
	bool Post(const std::string &sender, const char *name, const char *payload);
	// A mod's listen patterns: exact names, or a prefix ending in '*'.
	void Listen(const std::string &mod, const char *pattern);
	void Unlisten(const std::string &mod, const char *pattern);

	// Deliver the queue, oldest first; each notification to every mod that
	// listens for it but its sender, in the order of the mods' names, through
	// the loader that runs the receiver. What is posted meanwhile is delivered
	// in the same drain, for up to MAX_ROUNDS rounds or (finishing the
	// notification under way) MAX_DELIVERIES deliveries; the rest waits for the
	// next drain. A drain started from inside a delivery does nothing. Answers
	// how many deliveries it made.
	size_t Drain();
	bool Draining() const;
	// How many notifications wait, and their bytes (names and payloads).
	size_t Queued() const;
	size_t QueuedBytes() const;

	// Forget the queue (another pilot), or the queue and who listens (the end).
	void ClearQueue();
	void Reset();


private:
	struct Notification {
		std::string name;
		std::string payload;
		std::string sender;
	};

	static bool Matches(const std::set<std::string> &patterns, const std::string &name);
	static void LogProblem(const std::string &text);


private:
	ModLoaders &loaders;
	std::deque<Notification> queue;
	// The part of the queue a drain is delivering.
	std::deque<Notification> held;
	size_t queuedBytes = 0;
	std::map<std::string, std::set<std::string>> listeners;
	std::map<std::string, int> postsSinceDrain;
	bool draining = false;
};
