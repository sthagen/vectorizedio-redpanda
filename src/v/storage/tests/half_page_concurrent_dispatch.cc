#include "storage/record_batch_builder.h"
#include "storage/tests/utils/disk_log_builder.h"
#include "test_utils/fixture.h"

#include <seastar/core/temporary_buffer.hh>

struct fixture {
    storage::disk_log_builder b{storage::log_config{
      .base_dir = storage::random_dir(),
      .max_segment_size = (1 << 30),
      .should_sanitize = storage::log_config::sanitize_files::yes,
      .compaction_interval = std::chrono::minutes(1),
      .disable_cache = storage::log_config::disable_batch_cache::yes}};
    ~fixture() { b.stop().get(); }
};

FIXTURE_TEST(half_next_page, fixture) {
    using namespace storage; // NOLINT
    // gurantee next half page on 4096 segments(default)
    constexpr size_t data_size = (segment_appender::chunk_size / 2) + 1;
    ss::temporary_buffer<char> data(data_size);
    auto key = iobuf();
    auto value = iobuf();
    value.append(data.share());
    key.append(data.share());
    auto batchbldr = record_batch_builder(
      model::record_batch_type{1}, model::offset(0));
    auto batch = std::move(
                   batchbldr.add_raw_kv(std::move(key), std::move(value)))
                   .build();
    b | start() | add_segment(0);
    auto& seg = b.get_segment(0);
    info("About to append batch: {}", batch);
    seg.append(std::move(batch)).get();
    info("Segment: {}", seg);
    seg.flush().get();
    b | add_random_batch(1, 1, maybe_compress_batches::yes);
    auto recs = b.consume().get0();
    BOOST_REQUIRE_EQUAL(recs.size(), 2);
    for (auto& rec : recs) {
        BOOST_REQUIRE_EQUAL(rec.header().crc, model::crc_record_batch(rec));
    }
}
