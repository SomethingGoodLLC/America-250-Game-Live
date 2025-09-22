"""Backpressure utilities for providers."""

import asyncio
from typing import Any, Optional


class BoundedAIO:
    """Bounded async iterator with backpressure control.

    Small wrapper with asyncio.Queue(maxsize=N) + await put() + async for consumer.
    Used to avoid unbounded frame/text growth.
    """

    def __init__(self, maxsize: int = 100):
        if maxsize <= 0:
            raise ValueError("maxsize must be positive")
        self.queue: asyncio.Queue[Any] = asyncio.Queue(maxsize=maxsize)
        self.closed = False
        self._sentinel = object()  # Sentinel to signal end of stream

    async def put(self, item: Any) -> None:
        """Put an item in the queue with backpressure."""
        if self.closed:
            raise RuntimeError("BoundedAIO is closed")
        await self.queue.put(item)

    async def put_nowait(self, item: Any) -> bool:
        """Try to put an item without waiting. Returns True if successful."""
        if self.closed:
            raise RuntimeError("BoundedAIO is closed")
        try:
            self.queue.put_nowait(item)
            return True
        except asyncio.QueueFull:
            return False

    async def get(self) -> Any:
        """Get an item from the queue."""
        if self.closed and self.queue.empty():
            raise StopAsyncIteration
        
        item = await self.queue.get()
        if item is self._sentinel:
            raise StopAsyncIteration
        return item

    def get_nowait(self) -> Any:
        """Try to get an item without waiting."""
        if self.closed and self.queue.empty():
            raise StopAsyncIteration
        
        try:
            item = self.queue.get_nowait()
            if item is self._sentinel:
                raise StopAsyncIteration
            return item
        except asyncio.QueueEmpty:
            raise

    def __aiter__(self):
        return self

    async def __anext__(self) -> Any:
        """Async iterator protocol."""
        try:
            return await self.get()
        except StopAsyncIteration:
            raise

    async def close(self) -> None:
        """Close the bounded iterator and signal end of stream."""
        if not self.closed:
            self.closed = True
            # Put sentinel to wake up any waiting consumers
            try:
                await self.queue.put(self._sentinel)
            except asyncio.QueueFull:
                # If queue is full, we'll rely on the closed flag
                pass

    async def __aenter__(self):
        """Async context manager entry."""
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Async context manager exit."""
        await self.close()

    def qsize(self) -> int:
        """Get current queue size."""
        return self.queue.qsize()

    def empty(self) -> bool:
        """Check if queue is empty."""
        return self.queue.empty()

    def full(self) -> bool:
        """Check if queue is full."""
        return self.queue.full()

    @property
    def maxsize(self) -> int:
        """Get the maximum queue size."""
        return self.queue.maxsize
