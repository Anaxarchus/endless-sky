/* ModBus.cpp
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

#include "ModBus.h"

#include "ModLoaders.h"

#include "../Logger.h"

#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

using namespace std;



ModBus::ModBus(ModLoaders &loaders)
	: loaders(loaders)
{
}



bool ModBus::ValidName(const char *name, bool pattern)
{
	if(!name)
		return false;
	size_t length = strnlen(name, MAX_NAME + 1);
	if(length > MAX_NAME)
		return false;
	if(pattern && length && name[length - 1] == '*')
		--length;
	else if(!length)
		return false;
	for(size_t i = 0; i < length; ++i)
	{
		const char c = name[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
				|| c == '.' || c == '_' || c == ':' || c == '/' || c == '-'))
			return false;
	}
	return true;
}



bool ModBus::Post(const string &sender, const char *name, const char *payload)
{
	if(sender.empty())
		return false;
	if(!ValidName(name, false))
	{
		LogProblem("mod \"" + sender + "\" posted a notification with an invalid name; it was refused.");
		return false;
	}
	const size_t length = payload ? strnlen(payload, MAX_PAYLOAD + 1) : 0;
	if(length > MAX_PAYLOAD)
	{
		LogProblem("mod \"" + sender + "\" posted \"" + name + "\" with a payload over 64 KiB; it was refused.");
		return false;
	}
	const size_t bytes = strlen(name) + length;
	if(queue.size() + held.size() >= MAX_QUEUED || queuedBytes + bytes > MAX_QUEUED_BYTES)
	{
		LogProblem("the queue is full (" + to_string(MAX_QUEUED) + " notifications or "
			+ to_string(MAX_QUEUED_BYTES / (1024 * 1024)) + " MiB); \"" + name + "\" from mod \"" + sender
			+ "\" was dropped.");
		return false;
	}
	int &posts = postsSinceDrain[sender];
	if(posts >= MAX_POSTS_PER_DRAIN)
	{
		LogProblem("mod \"" + sender + "\" posted more than " + to_string(MAX_POSTS_PER_DRAIN)
			+ " notifications between deliveries; \"" + name + "\" was dropped.");
		return false;
	}
	++posts;
	queuedBytes += bytes;
	queue.push_back(Notification{name, string(payload ? payload : "", length), sender});
	return true;
}



void ModBus::Listen(const string &mod, const char *pattern)
{
	if(mod.empty())
		return;
	if(!ValidName(pattern, true))
	{
		LogProblem("mod \"" + mod + "\" listened for an invalid pattern; it was refused.");
		return;
	}
	set<string> &patterns = listeners[mod];
	if(patterns.size() >= MAX_PATTERNS && !patterns.count(pattern))
	{
		LogProblem("mod \"" + mod + "\" listens for " + to_string(MAX_PATTERNS) + " patterns already; \""
			+ pattern + "\" was refused.");
		return;
	}
	patterns.insert(pattern);
}



void ModBus::Unlisten(const string &mod, const char *pattern)
{
	if(!pattern)
		return;
	auto it = listeners.find(mod);
	if(it == listeners.end())
		return;
	it->second.erase(pattern);
	if(it->second.empty())
		listeners.erase(it);
}



size_t ModBus::Drain()
{
	if(draining)
		return 0;
	if(queue.empty())
	{
		postsSinceDrain.clear();
		return 0;
	}
	draining = true;
	size_t deliveries = 0;
	for(int round = 0; round < MAX_ROUNDS && !queue.empty() && deliveries < MAX_DELIVERIES; ++round)
	{
		held.swap(queue);
		while(!held.empty() && deliveries < MAX_DELIVERIES)
		{
			const Notification note = std::move(held.front());
			held.pop_front();
			queuedBytes -= note.name.size() + note.payload.size();
			// Who listens now: a receiver may listen or unlisten as it runs.
			vector<string> receivers;
			for(const auto &[mod, patterns] : listeners)
				if(mod != note.sender && Matches(patterns, note.name))
					receivers.push_back(mod);
			for(const string &mod : receivers)
			{
				++deliveries;
				loaders.ForMod(mod, [&](const EsLoader &loader) {
					if(ES_LOADER_HAS(&loader, notification) && loader.notification)
						loader.notification(mod.c_str(), note.name.c_str(), note.payload.c_str(), note.sender.c_str());
				});
			}
		}
		// What this round did not reach goes first, then what was posted during it.
		for(Notification &posted : queue)
			held.push_back(std::move(posted));
		queue.clear();
		queue.swap(held);
	}
	if(!queue.empty())
		LogProblem(to_string(queue.size()) + " notification(s) wait for the next delivery: this one made "
			+ to_string(deliveries) + " deliveries in " + to_string(MAX_ROUNDS) + " rounds or fewer, the most it makes.");
	postsSinceDrain.clear();
	draining = false;
	return deliveries;
}



bool ModBus::Draining() const
{
	return draining;
}



size_t ModBus::Queued() const
{
	return queue.size() + held.size();
}



size_t ModBus::QueuedBytes() const
{
	return queuedBytes;
}



void ModBus::ClearQueue()
{
	queue.clear();
	held.clear();
	queuedBytes = 0;
	postsSinceDrain.clear();
}



void ModBus::Reset()
{
	ClearQueue();
	listeners.clear();
}



bool ModBus::Matches(const set<string> &patterns, const string &name)
{
	for(const string &pattern : patterns)
	{
		if(!pattern.empty() && pattern.back() == '*')
		{
			if(name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0)
				return true;
		}
		else if(pattern == name)
			return true;
	}
	return false;
}



void ModBus::LogProblem(const string &text)
{
	// At most 20 lines a minute, so a mod that keeps posting badly cannot flood the log.
	static chrono::steady_clock::time_point windowStart;
	static bool started = false;
	static int logged = 0;
	static int suppressed = 0;
	const auto now = chrono::steady_clock::now();
	if(!started || now - windowStart > chrono::minutes(1))
	{
		if(suppressed)
			Logger::Log("Mod notifications: " + to_string(suppressed) + " more problem(s) were not logged.",
				Logger::Level::WARNING);
		started = true;
		windowStart = now;
		logged = 0;
		suppressed = 0;
	}
	if(logged >= 20)
	{
		++suppressed;
		return;
	}
	++logged;
	Logger::Log("Mod notifications: " + text, Logger::Level::WARNING);
}
