"""AgentExecutor —— 把 A2A 任务桥接到 core-engine 的 handle_task。

handle_task 是同步且含网络/LLM 调用的重操作,放到线程池执行,避免阻塞事件循环。
"""
from __future__ import annotations

import asyncio

from a2a.server.agent_execution import AgentExecutor, RequestContext
from a2a.server.events import EventQueue
from a2a.server.tasks import TaskUpdater
from a2a.types import Part, TextPart
from a2a.utils import new_task

from ..tasks import handle_task


class MoneyGodAgentExecutor(AgentExecutor):
    """接收自然语言任务,产出量化分析报告(已含风险提示)。"""

    async def execute(self, context: RequestContext, event_queue: EventQueue) -> None:
        query = context.get_user_input()

        task = context.current_task
        if task is None:
            task = new_task(context.message)
            await event_queue.enqueue_event(task)

        updater = TaskUpdater(event_queue, task.id, task.context_id)
        await updater.start_work()

        try:
            report = await asyncio.to_thread(handle_task, query)
            await updater.add_artifact(
                [Part(root=TextPart(text=report))],
                name="quant_analysis",
            )
            await updater.complete()
        except Exception as e:  # noqa: BLE001 保证任务始终收敛到终态
            await updater.failed(
                message=updater.new_agent_message(
                    [Part(root=TextPart(text=f"分析失败:{str(e)[:200]}"))]
                )
            )

    async def cancel(self, context: RequestContext, event_queue: EventQueue) -> None:
        raise Exception("MoneyGod 不支持取消正在进行的任务。")


__all__ = ["MoneyGodAgentExecutor"]
