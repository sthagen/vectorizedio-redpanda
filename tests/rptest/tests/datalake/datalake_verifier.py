# Copyright 2024 Redpanda Data, Inc.
#
# Use of this software is governed by the Business Source License
# included in the file licenses/BSL.md
#
# As of the Change Date specified in that file, in accordance with
# the Business Source License, use of this software will be governed
# by the Apache License, Version 2.0

from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
import random
import threading
from time import time, sleep
from typing import Optional
from rptest.clients.rpk import RpkTool
from rptest.services.redpanda import RedpandaService
from rptest.tests.datalake.query_engine_base import QueryEngineBase
from rptest.util import wait_until

from confluent_kafka import Consumer
from confluent_kafka import TopicPartition


class DatalakeVerifier():
    """
     Verifier that does the verification of the data in the redpanda Iceberg table.
     The verifier consumes offsets from specified topic and verifies it the data
     in the iceberg table matches.

     The verifier runs two threads:
     - one of them consumes messages from the specified topic and buffers them in memory.
       The semaphore is used to limit the number of messages buffered in memory.

     - second thread executes a per partition query that fetches the messages
       from the iceberg table
    """

    #TODO: add an ability to pass lambda to verify the message content
    def __init__(self,
                 redpanda: RedpandaService,
                 topic: str,
                 query_engine: QueryEngineBase,
                 compacted: bool = False):
        self.redpanda = redpanda
        self.topic = topic
        self.logger = redpanda.logger
        # Map from partition id to list of messages consumed
        # by from the partition
        self._consumed_messages = defaultdict(list)
        # Maximum offset consumed from the partition.
        # Consumed here refers to consumption by the app layer, meaning
        # there has to be a valid batch at the offset returned by the
        # kafka consume API.
        self._max_consumed_offsets = {}
        # Next position to be consumed from the partition. This may be
        # > max_consumed_offset + 1 if there are gaps from non consumable
        # batches like aborted data batches / control batches
        self._next_positions = defaultdict(lambda: -1)
        self._cg = f"verifier-group-{random.randint(0, 1000000)}"
        self._consumer: Consumer = self.create_consumer()
        self._query: QueryEngineBase = query_engine
        self._lock = threading.Lock()
        self._stop = threading.Event()
        # number of messages buffered in memory
        self._msg_semaphore = threading.Semaphore(5000)
        self._num_msgs_pending_verification = 0
        # Signalled when enough messages are batched so query
        # thread can perform verification. Larger batches results
        # in fewer SQL queries and hence faster verification
        self._msgs_batched = threading.Condition()
        self._query_batch_size = 1000
        self._query_batch_wait_timeout_s = 3
        self._executor = ThreadPoolExecutor(max_workers=2)
        self._rpk = RpkTool(self.redpanda)
        # errors found during verification
        self._errors = []
        # map of last queried offset for each partition
        self._max_queried_offsets = {}
        self._last_checkpoint = {}

        self._compacted = compacted
        # When consuming from a compacted topic, there may be records in the
        # Iceberg table that have since been compacted away in the log. We
        # maintain a set of compacted keys during message verification. If
        # the offset for a record read from the Iceberg table differs from
        # the offset for the record read from the log, the record's key is
        # added to the set. The key is removed from the set when a later
        # record with the same key is seen in the log. Finally, after
        # consuming, we assert that the size of this set is zero (otherwise,
        # it would imply an anomaly between the Iceberg table and the log).
        self._expected_compacted_keys = set()

    def create_consumer(self):
        c = Consumer({
            'bootstrap.servers': self.redpanda.brokers(),
            'group.id': self._cg,
            'auto.offset.reset': 'earliest'
        })
        c.subscribe([self.topic])

        return c

    def update_and_get_fetch_positions(self):
        with self._lock:
            partitions = [
                TopicPartition(topic=self.topic, partition=p)
                for p in self._consumed_messages.keys()
            ]
            positions = self._consumer.position(partitions)
            for p in positions:
                if p.error is not None:
                    self.logger.warning(
                        f"Erorr querying position for partition {p.partition}")
                else:
                    self.logger.debug(
                        f"next position for {p.partition} is {p.offset}")
                    self._next_positions[p.partition] = p.offset
            return self._next_positions.copy()

    def _consumer_thread(self):
        self.logger.info("Starting consumer thread")
        while not self._stop.is_set():
            self._msg_semaphore.acquire()
            if self._stop.is_set():
                break
            msg = self._consumer.poll(1.0)
            if msg is None:
                continue
            if msg.error():
                self.logger.error(f"Consumer error: {msg.error()}")
                continue

            with self._lock:
                self._num_msgs_pending_verification += 1
                self._consumed_messages[msg.partition()].append(msg)
                if self._num_msgs_pending_verification >= self._query_batch_size:
                    with self._msgs_batched:
                        self._msgs_batched.notify()
                self._max_consumed_offsets[msg.partition()] = max(
                    self._max_consumed_offsets.get(msg.partition(), -1),
                    msg.offset())
                if len(self._errors) > 0:
                    return

    def _get_query(self, partition, last_queried_offset, max_consumed_offset):
        return f"\
        SELECT redpanda.offset, redpanda.key FROM redpanda.{self._query.escape_identifier(self.topic)} \
        WHERE redpanda.partition={partition} \
        AND redpanda.offset>{last_queried_offset} \
        AND redpanda.offset<={max_consumed_offset} \
        ORDER BY redpanda.offset"

    def _verify_next_message(self, partition, iceberg_offset, iceberg_key):
        if partition not in self._consumed_messages:
            self._errors.append(
                f"Partition {partition} returned from Iceberg query  not found in consumed messages"
            )

        p_messages = self._consumed_messages[partition]

        if len(p_messages) == 0:
            return

        message = p_messages[0]
        consumer_offset = message.offset()
        if iceberg_offset > consumer_offset:
            self._errors.append(
                f"Offset from Iceberg table {iceberg_offset} is greater than next consumed offset {consumer_offset} for partition {partition}, most likely there is a gap in the table"
            )
            return

        if iceberg_offset <= self._max_queried_offsets.get(partition, -1):
            self._errors.append(
                f"Duplicate entry detected at offset {iceberg_offset} for partition {partition} "
            )
            return

        self._max_queried_offsets[partition] = iceberg_offset

        if consumer_offset != iceberg_offset:
            if self._compacted:
                self._expected_compacted_keys.add(iceberg_key)
                return
            else:
                self._errors.append(
                    f"Offset from iceberg table {iceberg_offset} for {partition} does not match the next consumed offset {consumer_offset}"
                )
                return
        else:
            if self._compacted:
                if iceberg_key in self._expected_compacted_keys:
                    self._expected_compacted_keys.remove(iceberg_key)

        self._consumed_messages[partition].pop(0)
        self._num_msgs_pending_verification -= 1
        self._msg_semaphore.release()

    def _query_thread(self):
        self.logger.info("Starting query thread")
        while not self._stop.is_set():
            try:
                with self._msgs_batched:
                    # Wait for enough data to be batched or a timeout.
                    self._msgs_batched.wait(
                        timeout=self._query_batch_wait_timeout_s)
                partitions = self.update_and_get_fetch_positions()

                for partition, next_consume_offset in partitions.items():
                    last_queried_offset = self._max_queried_offsets[
                        partition] if partition in self._max_queried_offsets else -1

                    max_consumed = next_consume_offset - 1
                    # no new messages consumed, skip query
                    if max_consumed <= last_queried_offset:
                        continue

                    query = self._get_query(partition, last_queried_offset,
                                            max_consumed)
                    self.logger.debug(f"Executing query: {query}")

                    with self._query.run_query(query) as cursor:
                        with self._lock:
                            for row in cursor:
                                self._verify_next_message(partition, *row)
                                if len(self._errors) > 0:
                                    self.logger.error(
                                        f"violations detected: {self._errors}, stopping verifier"
                                    )
                                    return

                    if len(self._max_queried_offsets) > 0:
                        self.logger.debug(
                            f"Max queried offsets: {self._max_queried_offsets}"
                        )

            except Exception as e:
                self.logger.error(f"Error querying iceberg table: {e}")
                sleep(2)

    def start(self):
        self._executor.submit(self._consumer_thread)
        self._executor.submit(self._query_thread)

    def _all_offsets_translated(self):
        partitions = self._rpk.describe_topic(self.topic)
        with self._lock:
            for p in partitions:
                if p.id not in self._max_queried_offsets:
                    self.logger.debug(
                        f"partition {p.id} not found in max offsets: {self._max_queried_offsets}"
                    )
                    return False

                if self._next_positions[p.id] < p.high_watermark:
                    self.logger.debug(
                        f"partition {p.id} high watermark: {p.high_watermark} max offset: {self._next_positions[p.id]} has not been consumed fully"
                    )
                    return False
                # Ensure all the consumed messages are drained.
                return all(
                    len(messages) == 0
                    for messages in self._consumed_messages.values())

        return True

    def _made_progress(self):
        progress = False
        with self._lock:
            for partition, offset in self._max_queried_offsets.items():
                if offset > self._last_checkpoint.get(partition, -1):
                    progress = True
                    break

            self._last_checkpoint = self._max_queried_offsets.copy()
        return progress

    def wait(self, progress_timeout_sec=30):
        try:
            while not self._all_offsets_translated():
                wait_until(
                    lambda: self._made_progress(),
                    progress_timeout_sec,
                    backoff_sec=3,
                    err_msg=
                    f"Error waiting for the query to make progress for topic {self.topic}"
                )
                assert len(
                    self._errors
                ) == 0, f"Topic {self.topic} validation errors: {self._errors}"

        finally:
            self.stop()

    def stop(self):
        try:
            self._stop.set()
            self._msg_semaphore.release()
            self._executor.shutdown(wait=False)
            assert len(
                self._errors
            ) == 0, f"Topic {self.topic} validation errors: {self._errors}"

            self.logger.debug(
                f"consumed offsets: {self._max_consumed_offsets}")
            self.logger.debug(f"queried offsets: {self._max_queried_offsets}")

            assert self._max_queried_offsets == self._max_consumed_offsets, "Mismatch between maximum offsets in topic vs iceberg table"

            assert len(
                self._expected_compacted_keys
            ) == 0, f"Some keys which were compacted away were not seen later in the consumer's log"
        finally:
            self._consumer.close()
