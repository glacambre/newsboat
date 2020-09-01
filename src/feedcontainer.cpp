#include "feedcontainer.h"

#include <algorithm> // stable_sort
#include <numeric>   // accumulate
#include <unordered_set>

#include "rssfeed.h"
#include "utils.h"

namespace newsboat {

void FeedContainer::sort_feeds(const FeedSortStrategy& sort_strategy)
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);

	switch (sort_strategy.sm) {
	case FeedSortMethod::NONE:
		std::stable_sort(feeds.begin(),
			feeds.end(),
			[](std::shared_ptr<RssFeed> a,
		std::shared_ptr<RssFeed> b) {
			return a->get_order() < b->get_order();
		});
		break;
	case FeedSortMethod::FIRST_TAG:
		std::stable_sort(feeds.begin(),
			feeds.end(),
			[](std::shared_ptr<RssFeed> a,
		std::shared_ptr<RssFeed> b) {
			if (a->get_firsttag().length() == 0 ||
				b->get_firsttag().length() == 0) {
				return a->get_firsttag().length() >
					b->get_firsttag().length();
			}
			return utils::strnaturalcmp(a->get_firsttag().c_str(),
					b->get_firsttag().c_str()) < 0;
		});
		break;
	case FeedSortMethod::TITLE:
		std::stable_sort(feeds.begin(),
			feeds.end(),
			[](std::shared_ptr<RssFeed> a,
		std::shared_ptr<RssFeed> b) {
			return utils::strnaturalcmp(a->title().c_str(),
					b->title().c_str()) < 0;
		});
		break;
	case FeedSortMethod::ARTICLE_COUNT:
		std::stable_sort(feeds.begin(),
			feeds.end(),
			[](std::shared_ptr<RssFeed> a,
		std::shared_ptr<RssFeed> b) {
			return a->total_item_count() <
				b->total_item_count();
		});
		break;
	case FeedSortMethod::UNREAD_ARTICLE_COUNT:
		std::stable_sort(feeds.begin(),
			feeds.end(),
			[](std::shared_ptr<RssFeed> a,
		std::shared_ptr<RssFeed> b) {
			return a->unread_item_count() <
				b->unread_item_count();
		});
		break;
	case FeedSortMethod::LAST_UPDATED:
		std::stable_sort(feeds.begin(),
			feeds.end(),
			[](std::shared_ptr<RssFeed> a,
		std::shared_ptr<RssFeed> b) {
			if (a->items().size() == 0 ||
				b->items().size() == 0) {
				return a->items().size() >
					b->items().size();
			}
			auto cmp =
				[](std::shared_ptr<RssItem> a,
			std::shared_ptr<RssItem> b) {
				return *a < *b;
			};
			auto& a_item =
				*std::min_element(a->items().begin(),
					a->items().end(),
					cmp);
			auto& b_item =
				*std::min_element(b->items().begin(),
					b->items().end(),
					cmp);
			return cmp(a_item, b_item);
		});
		break;
	}

	switch (sort_strategy.sd) {
	case SortDirection::ASC:
		std::reverse(feeds.begin(), feeds.end());
		break;
	case SortDirection::DESC:
		break;
	}
}

std::shared_ptr<RssFeed> FeedContainer::get_feed(const unsigned int pos)
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	if (pos >= feeds.size()) {
		throw std::out_of_range(_("invalid feed index (bug)"));
	}
	std::shared_ptr<RssFeed> feed = feeds[pos];
	return feed;
}

void FeedContainer::mark_all_feed_items_read(const unsigned int feed_pos)
{
	const auto feed = get_feed(feed_pos);
	std::lock_guard<std::mutex> lock(feed->item_mutex);
	std::vector<std::shared_ptr<RssItem>>& items = feed->items();
	if (items.size() > 0) {
		bool notify = items[0]->feedurl() != feed->rssurl();
		LOG(Level::DEBUG,
			"FeedContainer::mark_all_read: notify = %s",
			notify ? "yes" : "no");
		for (const auto& item : items) {
			item->set_unread_nowrite_notify(false, notify);
		}
	}
}

void FeedContainer::mark_all_feeds_read()
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	for (const auto& feed : feeds) {
		feed->mark_all_items_read();
	}
}

void FeedContainer::add_feed(const std::shared_ptr<RssFeed> feed)
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	feeds.push_back(feed);
}

void FeedContainer::populate_query_feeds()
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	for (const auto& feed : feeds) {
		if (feed->is_query_feed()) {
			feed->update_items(feeds);
		}
	}
}

unsigned int FeedContainer::get_feed_count_per_tag(const std::string& tag)
{
	unsigned int count = 0;
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	for (const auto& feed : feeds) {
		if (feed->matches_tag(tag)) {
			count++;
		}
	}

	return count;
}

unsigned int FeedContainer::get_unread_feed_count_per_tag(
	const std::string& tag)
{
	unsigned int count = 0;
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	for (const auto& feed : feeds) {
		if (feed->matches_tag(tag) && feed->unread_item_count() > 0) {
			count++;
		}
	}

	return count;
}

unsigned int FeedContainer::get_unread_item_count_per_tag(
	const std::string& tag)
{
	unsigned int count = 0;
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	for (const auto& feed : feeds) {
		if (feed->matches_tag(tag)) {
			count += feed->unread_item_count();
		}
	}

	return count;
}

std::shared_ptr<RssFeed> FeedContainer::get_feed_by_url(
	const std::string& feedurl)
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	for (const auto& feed : feeds) {
		if (feedurl == feed->rssurl()) {
			return feed;
		}
	}
	LOG(Level::ERROR,
		"FeedContainer:get_feed_by_url failed for %s",
		feedurl);
	return std::shared_ptr<RssFeed>();
}

unsigned int FeedContainer::get_pos_of_next_unread(unsigned int pos)
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	for (pos++; pos < feeds.size(); pos++) {
		if (feeds[pos]->unread_item_count() > 0) {
			break;
		}
	}
	return pos;
}

unsigned int FeedContainer::feeds_size()
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	return feeds.size();
}

void FeedContainer::reset_feeds_status()
{
	std::lock_guard<std::mutex> feedlock(feeds_mutex);
	for (const auto& feed : feeds) {
		feed->reset_status();
	}
}

void FeedContainer::set_feeds(
	const std::vector<std::shared_ptr<RssFeed>> new_feeds)
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	feeds = new_feeds;
}

std::vector<std::shared_ptr<RssFeed>> FeedContainer::get_all_feeds()
{
	std::vector<std::shared_ptr<RssFeed>> tmpfeeds;
	{
		std::lock_guard<std::mutex> feedslock(feeds_mutex);
		tmpfeeds = feeds;
	}
	return tmpfeeds;
}

unsigned int FeedContainer::unread_feed_count() const
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	return std::count_if(feeds.begin(),
			feeds.end(),
	[](const std::shared_ptr<RssFeed> feed) {
		return feed->unread_item_count() > 0;
	});
}

unsigned int FeedContainer::unread_item_count() const
{
	std::lock_guard<std::mutex> feedslock(feeds_mutex);

	using guid_set = std::unordered_set<std::string>;
	const auto unread_guids =
		std::accumulate(feeds.begin(),
			feeds.end(),
			guid_set(),
	[](guid_set guids, const std::shared_ptr<RssFeed> feed) {
		// Hidden feeds can't be viewed. The only way to read their articles is
		// via a query feed; items that aren't in query feeds are completely
		// inaccessible. Thus, we skip hidden feeds altogether to avoid
		// counting items that can't be accessed.
		if (feed->hidden()) {
			return guids;
		}

		std::lock_guard<std::mutex> itemslock(feed->item_mutex);
		for (const auto& item : feed->items()) {
			if (item->unread()) {
				guids.insert(item->guid());
			}
		}

		return guids;
	});

	return unread_guids.size();
}

void FeedContainer::replace_feed(unsigned int pos,
	std::shared_ptr<RssFeed> feed)
{
	assert(pos < feeds.size());

	std::lock_guard<std::mutex> feedslock(feeds_mutex);
	feeds[pos] = feed;
}

} // namespace newsboat
